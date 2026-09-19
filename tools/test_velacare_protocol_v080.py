#!/usr/bin/env python3
"""VelaCare D12X/ESP32 host protocol regression (business sources are read-only)."""
from __future__ import annotations

import argparse
import copy
import hashlib
import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable, Dict, List, Optional, Sequence, Tuple

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
REPORT = HERE / "PROTOCOL_AUDIT_20260918.md"
PATHS = {
    "D12X sensor": ROOT / "tmp/v070-freeze-fix-src/app/velacare/velacare_sensor.c",
    "D12X header": ROOT / "tmp/v070-freeze-fix-src/app/velacare/velacare_sensor.h",
    "D12X settings": ROOT / "tmp/v070-freeze-fix-src/app/velacare/velacare_settings.c",
    "D12X settings header": ROOT / "tmp/v070-freeze-fix-src/app/velacare/velacare_settings.h",
    "D12X LVGL": ROOT / "tmp/v070-freeze-fix-src/app/velacare/velacare_lvgl.c",
    "D12X LVGL header": ROOT / "tmp/v070-freeze-fix-src/app/velacare/velacare_lvgl.h",
    "D12X main": ROOT / "tmp/v070-freeze-fix-src/app/velacare/velacare_main.c",
    "ESP32 gateway": ROOT / "esp32_velacare_gateway/esp32_velacare_gateway.ino",
}

FAMILY_NOTIFICATION_CAPACITY = 12


def crc16(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def make_frame(body: str) -> str:
    return f"{body}*{crc16(body.encode('ascii')):04X}\n"


def parse_frame(raw: str) -> str:
    found = re.fullmatch(r"([^*\r\n]+)\*([0-9A-Fa-f]{4})\r?\n?", raw)
    if not found:
        raise ValueError("malformed frame")
    body, check = found.groups()
    if crc16(body.encode("ascii")) != int(check, 16):
        raise ValueError("CRC mismatch")
    return body


class IdAllocator:
    """Persisted high-water allocator; no persistence means no request ID."""
    def __init__(self, nv: Optional[Dict[str, int]] = None, seed: int = 0x43800000):
        if nv is None:
            raise OSError("request-ID persistence unavailable")
        self.nv = nv
        first = self.nv.get("next", 0) or ((seed & 0x7FFFFFFF) | 0x10000000)
        self.next, self.limit = first, first + 256
        self.nv["next"] = self.limit

    def take(self) -> int:
        if self.next >= self.limit:
            self.next = self.nv["next"]
            self.limit = self.next + 256
            self.nv["next"] = self.limit
        value, self.next = self.next, self.next + 1
        assert value != 0
        return value


@dataclass
class D12X:
    ids: IdAllocator
    state_nv: Optional[dict] = None
    persistence_enabled: bool = False
    sensors: List[int] = field(default_factory=lambda: [0, 0, 0, 0])
    muted: bool = False
    acknowledged: bool = False
    sos_id: int = 0
    sos_active: bool = False
    sos_cancel: bool = False
    sos_status: str = "IDLE"
    care_id: int = 0
    care_type: int = 0
    care_active: bool = False
    care_cancel: bool = False
    care_status: str = "IDLE"
    sos_reply: int = 0
    care_reply: int = 0
    fam_history: List[Tuple[int, int]] = field(default_factory=list)

    def __post_init__(self) -> None:
        if self.persistence_enabled and self.state_nv and "protocol" in self.state_nv:
            saved = copy.deepcopy(self.state_nv["protocol"])
            for key, value in saved.items():
                setattr(self, key, value)

    def _save(self) -> None:
        if self.persistence_enabled and self.state_nv is not None:
            names = ("sos_id", "sos_active", "sos_cancel", "sos_status",
                     "sos_reply",
                     "care_id", "care_type", "care_active", "care_cancel",
                     "care_status", "care_reply", "fam_history")
            self.state_nv["protocol"] = copy.deepcopy({name: getattr(self, name) for name in names})

    def send_sos(self) -> str:
        assert not self.sos_active
        self.sos_id, self.sos_active, self.sos_status = self.ids.take(), True, "WAIT_GATEWAY"
        self.sos_reply = 0
        self._save()
        return make_frame(f"SOS1,{self.sos_id}")

    def cancel_sos(self) -> str:
        assert self.sos_active and self.sos_id
        self.sos_cancel, self.sos_status = True, "CANCELLING"
        self._save()
        return make_frame(f"SOSX1,{self.sos_id}")

    def send_care(self, kind: int) -> str:
        assert kind in (1, 2) and not self.care_active
        self.care_id, self.care_type = self.ids.take(), kind
        self.care_active, self.care_cancel, self.care_status = True, False, "WAIT_GATEWAY"
        self.care_reply = 0
        self._save()
        return make_frame(f"CARE1,{self.care_id},{kind}")

    def cancel_care(self) -> str:
        assert self.care_active and self.care_type in (1, 2)
        self.care_cancel, self.care_status = True, "CANCELLING"
        self._save()
        return make_frame(f"CAREX1,{self.care_id},{self.care_type}")

    def confirm_safe(self) -> str:
        return make_frame(f"OK1,{self.ids.take()}")

    def vc1(self, *states: int) -> None:
        self.sensors[:] = states

    def vcb1(self, muted: int, acknowledged: int, _severity: int) -> None:
        self.muted, self.acknowledged = bool(muted), bool(acknowledged)

    def _remember(self, key: Tuple[int, int]) -> None:
        if key not in self.fam_history:
            if len(self.fam_history) == FAMILY_NOTIFICATION_CAPACITY:
                self.fam_history.pop(0)
            self.fam_history.append(key)
        self._save()

    def receive(self, raw: str) -> List[str]:
        try:
            p = parse_frame(raw).split(",")
            if len(p) == 3 and p[0] == "SACK1":
                rid, status = int(p[1]), int(p[2])
                if rid == self.sos_id and self.sos_active and status in (1, 2):
                    if not self.sos_cancel:
                        self.sos_status = "NO_CAREGIVER" if status == 1 else "WAIT_FAMILY"
                    self._save()
                return []
            if len(p) == 3 and p[0] == "SXACK1":
                rid, status = int(p[1]), int(p[2])
                if rid == self.sos_id and self.sos_active and self.sos_cancel and status in (0, 1, 2):
                    self.sos_active = self.sos_cancel = False
                    self.sos_status = "FAMILY_SEEN" if status == 2 else "CANCELLED"
                    self._save()
                return []
            if len(p) == 4 and p[0] == "CACK1":
                rid, kind, status = map(int, p[1:])
                valid = (rid == self.care_id and kind == self.care_type and self.care_active
                         and kind in (1, 2) and status in (1, 2))
                if valid and not self.care_cancel:
                    self.care_status = "NO_CAREGIVER" if status == 1 else "WAIT_FAMILY"
                    self._save()
                return []
            if len(p) == 4 and p[0] == "CXACK1":
                rid, kind, status = map(int, p[1:])
                valid = (rid == self.care_id and kind == self.care_type and self.care_active
                         and self.care_cancel and kind in (1, 2) and status in (0, 1, 2))
                if valid:
                    self.care_active = self.care_cancel = False
                    self.care_status = "FAMILY_SEEN" if status == 2 else "CANCELLED"
                    self._save()
                return []
            if len(p) == 4 and p[0] == "FAM1":
                rid, kind, reply = map(int, p[1:])
                if kind not in (0, 1, 2) or reply not in (1, 2, 3, 4):
                    return []
                key = (rid, kind)
                if key in self.fam_history:
                    return [make_frame(f"FACK1,{rid},{kind}")]
                if kind == 0:
                    if rid != self.sos_id or not self.sos_active or self.sos_status in ("IDLE", "CANCELLED"):
                        return []
                    self.sos_active = self.sos_cancel = False
                    self.sos_status = "FAMILY_SEEN"
                    self.sos_reply = reply
                else:
                    valid = (rid == self.care_id and kind == self.care_type and self.care_active
                             and self.care_status not in ("IDLE", "CANCELLED"))
                    if not valid:
                        return []
                    self.care_active = self.care_cancel = False
                    self.care_status, self.care_reply = "FAMILY_SEEN", reply
                self._remember(key)
                self._save()
                return [make_frame(f"FACK1,{rid},{kind}")]
        except ValueError:
            pass
        return []


class Gateway:
    CAPACITY = FAMILY_NOTIFICATION_CAPACITY

    def __init__(self, nv: Optional[dict] = None, caregiver: bool = True):
        self.nv = nv if nv is not None else {}
        self.caregiver = caregiver
        self.active: Dict[int, int] = copy.deepcopy(self.nv.get("active", {}))
        self.history: Dict[Tuple[int, int], str] = copy.deepcopy(self.nv.get("history", {}))
        self.queue: List[Tuple[int, int, int]] = copy.deepcopy(self.nv.get("queue", []))
        self.sensors = [0, 0, 0, 0]
        self.muted = self.acknowledged = False
        self._save()

    def _save(self) -> None:
        self.nv["active"] = copy.deepcopy(self.active)
        self.nv["history"] = copy.deepcopy(self.history)
        self.nv["queue"] = copy.deepcopy(self.queue)

    def pending(self) -> List[str]:
        return [make_frame(f"FAM1,{rid},{kind},{reply}") for rid, kind, reply in self.queue]

    def family_reply(self, kind: int, rid: int, reply: int) -> bool:
        if kind not in (0, 1, 2) or reply not in (1, 2, 3, 4) or self.active.get(kind) != rid:
            return False
        index = next((i for i, item in enumerate(self.queue) if item[:2] == (rid, kind)), None)
        if index is None:
            if len(self.queue) >= self.CAPACITY:
                return False
            self.queue.append((rid, kind, reply))
        else:
            self.queue[index] = (rid, kind, reply)
        self.history[(kind, rid)] = "SEEN"
        self.active.pop(kind, None)
        self._save()
        return True

    def _cancel(self, kind: int, rid: int) -> int:
        if self.active.get(kind) == rid:
            self.active.pop(kind, None)
            self.history[(kind, rid)] = "CANCELLED"
            self._save()
            return 1
        outcome = self.history.get((kind, rid))
        return 2 if outcome == "SEEN" else 1 if outcome == "CANCELLED" else 0

    def receive(self, raw: str) -> List[str]:
        try:
            p = parse_frame(raw).split(",")
            if len(p) == 2 and p[0] == "SOS1":
                rid = int(p[1])
                if not rid:
                    return []
                if self.active.get(0) != rid and (0, rid) not in self.history:
                    self.active[0], self.history[(0, rid)] = rid, "ACTIVE"
                    self._save()
                result = [make_frame(f"SACK1,{rid},{2 if self.caregiver else 1}")]
                if self.history.get((0, rid)) == "SEEN":
                    result += [x for x in self.pending() if parse_frame(x).split(",")[1:3] == [str(rid), "0"]]
                return result
            if len(p) == 3 and p[0] == "CARE1":
                rid, kind = int(p[1]), int(p[2])
                if not rid or kind not in (1, 2):
                    return []
                if self.active.get(kind) != rid and (kind, rid) not in self.history:
                    self.active[kind], self.history[(kind, rid)] = rid, "ACTIVE"
                    self._save()
                result = [make_frame(f"CACK1,{rid},{kind},{2 if self.caregiver else 1}")]
                if self.history.get((kind, rid)) == "SEEN":
                    result += [x for x in self.pending() if parse_frame(x).split(",")[1:3] == [str(rid), str(kind)]]
                return result
            if len(p) == 2 and p[0] == "SOSX1":
                rid = int(p[1])
                return [make_frame(f"SXACK1,{rid},{self._cancel(0, rid)}")]
            if len(p) == 3 and p[0] == "CAREX1":
                rid, kind = int(p[1]), int(p[2])
                if kind in (1, 2):
                    return [make_frame(f"CXACK1,{rid},{kind},{self._cancel(kind, rid)}")]
            if len(p) == 3 and p[0] == "FACK1":
                rid, kind = int(p[1]), int(p[2])
                for index, item in enumerate(self.queue):
                    if item[:2] == (rid, kind):
                        self.queue.pop(index)
                        self._save()
                        break
                return []
        except (ValueError, TypeError):
            pass
        return []


@dataclass
class Result:
    name: str
    status: str
    detail: str
    known_gap: bool = False


def load_sources() -> Dict[str, str]:
    missing = [str(path) for path in PATHS.values() if not path.is_file()]
    if missing:
        raise FileNotFoundError("missing source: " + ", ".join(missing))
    return {name: path.read_text(encoding="utf-8", errors="replace") for name, path in PATHS.items()}


def _braced_span(source: str, anchor: str, start: int = 0) -> Tuple[int, int]:
    """Return the inclusive brace span following anchor, respecting nesting."""
    anchor_at = source.find(anchor, start)
    if anchor_at < 0:
        raise ValueError(f"anchor not found: {anchor}")
    open_at = source.find("{", anchor_at + len(anchor))
    if open_at < 0:
        raise ValueError(f"opening brace not found after: {anchor}")
    depth = 0
    for index in range(open_at, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return open_at, index
    raise ValueError(f"unterminated block after: {anchor}")


def _braced_body(source: str, anchor: str, start: int = 0) -> str:
    opening, closing = _braced_span(source, anchor, start)
    return source[opening + 1:closing]


def _constant_value(source: str, name: str) -> int:
    patterns = (
        rf"#define\s+{re.escape(name)}\s+(\d+)[uUlL]*\b",
        rf"\b{name}\s*=\s*(\d+)[uUlL]*\s*;",
    )
    for pattern in patterns:
        found = re.search(pattern, source)
        if found:
            return int(found.group(1))
    raise ValueError(f"integer constant not found: {name}")


def _fam_branch_bodies(sensor: str) -> Tuple[str, str, str]:
    fam_open, fam_close = _braced_span(
        sensor, 'if (strncmp(line, "FAM1,", 5) == 0)')
    fam = sensor[fam_open + 1:fam_close]
    sos_open, sos_close = _braced_span(fam, "if (event_type == 0)")
    else_at = fam.find("else", sos_close + 1)
    if else_at < 0:
        raise ValueError("FAM care branch not found")
    care_open, care_close = _braced_span(fam, "else", else_at)
    return (fam[sos_open + 1:sos_close],
            fam[care_open + 1:care_close], fam[care_close + 1:])


def _reply_fields(settings_h: str) -> Tuple[str, str]:
    declarations = re.findall(
        r"\b(?:u?int(?:8|16|32|64)_t|int)\s+([A-Za-z_]\w*reply\w*)\s*;",
        settings_h)
    sos = next((name for name in declarations if "sos" in name.lower()), "")
    care = next((name for name in declarations if "care" in name.lower()), "")
    if not sos or not care or sos == care:
        raise ValueError(
            "request state needs distinct SOS and CARE reply fields; found=" +
            repr(declarations))
    return sos, care


def durable_request_id_contract(sensor: str, settings: str) -> Tuple[bool, str]:
    forbidden = (
        "velacare_sensor_use_ephemeral_request_ids",
        "using nonzero ephemeral",
        "ephemeral request id",
    )
    present = [token for token in forbidden if token.lower() in sensor.lower()]
    allocator_persisted = (
        "velacare_settings_reserve_request_ids(" in sensor and
        "velacare_settings_reserve_request_ids(" in settings and
        "g_settings.next_request_id = limit" in settings)
    valid = not present and allocator_persisted
    detail = (f"forbidden={present or 'none'}; "
              f"persisted_reservation={allocator_persisted}")
    return valid, detail


def _settings_read_regions(settings: str, init_body: str) -> List[str]:
    regions = [init_body]
    calls = set(re.findall(r"\b(settings_[A-Za-z_]\w*)\s*\(", init_body))
    for name in calls:
        if name in ("settings_defaults", "settings_save"):
            continue
        try:
            regions.append(_braced_body(settings, name + "("))
        except ValueError:
            pass
    return regions


def settings_fail_closed_contract(settings: str) -> Tuple[bool, str]:
    """Require only ENOENT initialization and robust, fail-closed reads."""
    try:
        init_body = _braced_body(settings, "int velacare_settings_init(")
        request_body = _braced_body(
            settings, "int velacare_settings_load_request_state(")
        read_body = _braced_body(settings, "static int settings_read_file(")
        read_open_error = _braced_body(read_body, "if (fd < 0)")
        init_missing = _braced_body(init_body, "if (ret == -ENOENT)")
        _, init_missing_close = _braced_span(
            init_body, "if (ret == -ENOENT)")
        init_after_missing = init_body[init_missing_close + 1:]
        init_read_error = _braced_body(init_after_missing, "if (ret < 0)")
        request_missing = _braced_body(request_body, "if (ret == -ENOENT)")
        request_read_error = _braced_body(request_body, "if (ret < 0)")
    except ValueError as exc:
        return False, str(exc)

    helper_open_safe = re.search(r"return\s+-errno\s*;", read_open_error) is not None
    only_enoent_init = (
        "settings_save(" in init_missing and
        re.search(r"return\s+ret\s*;", init_read_error) is not None and
        "settings_save(" not in init_read_error)

    eintr_safe = (
        "read(" in read_body and "EINTR" in read_body and
        ("while" in read_body or "for" in read_body) and
        re.search(r"return\s+-error\s*;", read_body) is not None)

    # The helper may legitimately return the number of bytes read. A short
    # file is still fail-closed when every caller accepts only an exact known
    # structure size and otherwise returns an error without rewriting it.
    init_exact_sizes = (
        "*length = total" in read_body and
        "length == sizeof(stored)" in init_body and
        "length == sizeof(legacy)" in init_body)
    request_exact_sizes = (
        "length == sizeof(stored)" in request_body and
        "length == sizeof(legacy)" in request_body)
    oversized_fails = ("EOVERFLOW" in read_body and
                       re.search(r"nread\s*!=\s*0", read_body) is not None)
    short_read_safe = init_exact_sizes and request_exact_sizes and oversized_fails

    tail = init_body.rstrip()[-700:]
    invalid_fails = (
        "restoring defaults" not in init_body.lower() and
        re.search(r"return\s+-(?:E[A-Z0-9_]+|error)\s*;", tail) is not None and
        init_body.count("settings_save(") <= 2)

    request_safe = (
        re.search(r"return\s+0\s*;", request_missing) is not None and
        re.search(r"return\s+ret\s*;", request_read_error) is not None and
        "settings_save(" not in request_body and
        "request_state_checksum" in request_body and
        "request_state_payload_valid" in request_body and
        re.search(r"return\s+-E(?:INVAL|IO|BADMSG)", request_body) is not None)

    valid = (helper_open_safe and only_enoent_init and eintr_safe and
             short_read_safe and invalid_fails and request_safe)
    detail = (f"helper_open_fails={helper_open_safe}; "
              f"only_ENOENT_init={only_enoent_init}; EINTR_loop={eintr_safe}; "
              f"short_read_fails={short_read_safe}; corrupt_fails={invalid_fails}; "
              f"request_state_fails_closed={request_safe}")
    return valid, detail


def family_history_capacity_contract(settings_h: str,
                                     gateway: str) -> Tuple[bool, str]:
    try:
        d12x_capacity = _constant_value(
            settings_h, "VELACARE_REQUEST_FAMILY_HISTORY")
        esp_capacity = _constant_value(gateway, "kFamilyNotificationCapacity")
    except ValueError as exc:
        return False, str(exc)
    valid = (esp_capacity >= FAMILY_NOTIFICATION_CAPACITY and
             d12x_capacity >= esp_capacity)
    return valid, f"D12X={d12x_capacity}; ESP32={esp_capacity}"


def separated_reply_contract(sources: Dict[str, str]) -> Tuple[bool, str]:
    sensor = sources["D12X sensor"]
    settings = sources["D12X settings"]
    settings_h = sources["D12X settings header"]
    try:
        sos_field, care_field = _reply_fields(settings_h)
        sos_branch, care_branch, _ = _fam_branch_bodies(sensor)
        send_sos = _braced_body(sensor, "int velacare_sensor_send_sos(")
        send_care = _braced_body(sensor, "int velacare_sensor_send_care(")
    except ValueError as exc:
        return False, str(exc)

    def reply_assignment(body: str, field: str) -> bool:
        return re.search(
            rf"g_snapshot\.{re.escape(field)}\s*=\s*(?:\(int\)\s*)?reply_code",
            body) is not None

    assignments = (reply_assignment(sos_branch, sos_field) and
                   reply_assignment(care_branch, care_field))
    resets = (re.search(rf"g_snapshot\.{re.escape(sos_field)}\s*=\s*0", send_sos)
              is not None and
              re.search(rf"g_snapshot\.{re.escape(care_field)}\s*=\s*0", send_care)
              is not None)
    persisted = all(
        sensor.count("state->" + field) >= 2 and
        sensor.count("g_snapshot." + field) >= 3 and
        settings.count("state->" + field) >= 2
        for field in (sos_field, care_field))
    valid = assignments and resets and persisted
    detail = (f"fields={sos_field}/{care_field}; assignments={assignments}; "
              f"resets={resets}; persisted={persisted}")
    return valid, detail


def sos_ui_reply_contract(sources: Dict[str, str]) -> Tuple[bool, str]:
    settings_h = sources["D12X settings header"]
    sensor_h = sources["D12X header"]
    lvgl = sources["D12X LVGL"]
    lvgl_h = sources["D12X LVGL header"]
    main = sources["D12X main"]
    try:
        sos_field, _ = _reply_fields(settings_h)
        help_body = _braced_body(lvgl, "static void apply_help_flow(")
    except ValueError as exc:
        return False, str(exc)

    seen_at = help_body.find("case VELACARE_SOS_FAMILY_SEEN:")
    next_case = help_body.find("case ", seen_at + 1) if seen_at >= 0 else -1
    seen_body = help_body[seen_at:next_case if next_case >= 0 else len(help_body)]
    reply_tokens = [token for token in re.findall(r"\b[A-Za-z_]\w*\b", seen_body)
                    if "sos" in token.lower() and "reply" in token.lower()]
    rendered = ("help_notice" in seen_body and
                "family_reply_text(" in seen_body and bool(reply_tokens))
    reply_texts = all(text in lvgl for text in (
        "已经看到", "马上联系", "正在赶来", "请您先休息"))

    getter_names = re.findall(
        r"\b(velacare_sensor_\w*sos\w*reply\w*)\s*\(",
        sensor_h, re.IGNORECASE)
    getter_wired = any(name + "(" in main for name in getter_names)
    lvgl_wired = (sos_field in lvgl and
                  re.search(r"\bsos\w*reply\w*\b", lvgl_h,
                            re.IGNORECASE) is not None)
    valid = rendered and reply_texts and getter_wired and lvgl_wired
    detail = (f"field={sos_field}; rendered={rendered}; reply_texts={reply_texts}; "
              f"sensor_to_main={getter_wired}; main_to_lvgl={lvgl_wired}")
    return valid, detail


def fam_persistence_contract(sensor: str) -> Tuple[bool, str]:
    """Verify both first-time FAM paths persist their final state before FACK."""
    try:
        sos_body, care_body, common_tail = _fam_branch_bodies(sensor)
    except ValueError as exc:
        return False, str(exc)

    branches = (
        ("SOS", sos_body,
         ("g_sos_active = false", "g_sos_acked = true",
          "g_snapshot.sos_status = VELACARE_SOS_FAMILY_SEEN")),
        ("CARE", care_body,
         ("g_care_active = false", "g_care_acked = true",
          "g_snapshot.care_status = VELACARE_CARE_FAMILY_SEEN")),
    )
    details: List[str] = []
    all_valid = True
    for name, body, required_mutations in branches:
        # Discard the stale/duplicate-ID guard. Its direct FACK is safe because
        # that key was already committed by an earlier first-time delivery.
        guard_end = body.rfind("return false;")
        accepted_path = body[guard_end + len("return false;"):] + common_tail
        remember_at = accepted_path.find("velacare_sensor_remember_family_ack(")
        commit_at = accepted_path.find("velacare_sensor_commit_request_state(")
        fack_at = accepted_path.find("velacare_sensor_write_family_ack(")
        mutation_positions = [accepted_path.find(token) for token in required_mutations]
        reply_match = re.search(
            r"g_snapshot\.[A-Za-z_]\w*reply\w*\s*=\s*"
            r"(?:\(int\)\s*)?reply_code", accepted_path)
        reply_at = reply_match.start() if reply_match else -1
        mutation_positions.append(reply_at)
        branch_valid = (guard_end >= 0 and remember_at >= 0 and commit_at >= 0 and
                        fack_at >= 0 and all(position >= 0 for position in mutation_positions) and
                        max([remember_at] + mutation_positions) < commit_at < fack_at)
        all_valid = all_valid and branch_valid
        details.append(
            f"{name}=" + ("persist-before-FACK" if branch_valid else
                           f"invalid(order remember={remember_at}, commit={commit_at}, "
                           f"fack={fack_at}, mutations={mutation_positions})"))
    return all_valid, "; ".join(details)


def persistence_capability(sources: Dict[str, str]) -> Tuple[bool, str]:
    """Recognize complete, crash-safe SOS/care/FAM runtime persistence."""
    sensor = sources["D12X sensor"]
    settings = sources["D12X settings"]
    settings_h = sources["D12X settings header"]
    load_name = "velacare_settings_load_request_state"
    save_name = "velacare_settings_save_request_state"
    api_defined = (f"int {load_name}(" in settings and f"int {save_name}(" in settings
                   and load_name in settings_h and save_name in settings_h)
    # Definitions alone are insufficient: the sensor runtime must call both.
    load_calls = sensor.count(load_name + "(")
    save_calls = sensor.count(save_name + "(")
    fields = ("sos_request_id", "care_request_id", "family_history")
    state_complete = all(field in settings_h for field in fields)
    fam_safe, fam_detail = fam_persistence_contract(sensor)
    capable = (api_defined and load_calls > 0 and save_calls > 0 and
               state_complete and fam_safe)
    detail = (f"api_defined={api_defined}; sensor_load_calls={load_calls}; "
              f"sensor_save_calls={save_calls}; state_complete={state_complete}; "
              f"fam_safe={fam_safe} ({fam_detail})")
    return capable, detail


def tests(sources: Dict[str, str]) -> Sequence[Tuple[str, Callable[[], None], bool]]:
    sensor, header = sources["D12X sensor"], sources["D12X header"]
    settings = sources["D12X settings"]
    settings_h = sources["D12X settings header"]
    gateway = sources["ESP32 gateway"]
    persistent, persistent_detail = persistence_capability(sources)

    def crc_codec() -> None:
        assert crc16(b"123456789") == 0x29B1
        encoded = make_frame("SOS1,123")
        assert parse_frame(encoded) == "SOS1,123"
        try:
            parse_frame(encoded[:-5] + "0000\n")
        except ValueError:
            return
        raise AssertionError("corrupt CRC accepted")

    def static_contract() -> None:
        for token in ("SOS1,%lu", "SOSX1,%lu", "CARE1,%lu,%d", "CAREX1,%lu,%d", "FACK1,%lu,%u"):
            assert token in sensor, token
        for token in ("SACK1,%lu,%u", "SXACK1,%lu,%u", "CACK1,%lu,%u,%u", "CXACK1,%lu,%u,%u", "FAM1,%lu,%u,%u"):
            assert token in gateway, token
        assert "request_id != g_snapshot.sos_request_id" in sensor
        assert "event_type != (unsigned int)g_snapshot.care_kind" in sensor
        assert "reply_code < 1 || reply_code > 4" in sensor
        assert "kFamilyNotificationCapacity = 12" in gateway
        assert 'preferences.begin("velafamily", false)' in gateway
        assert "g_settings.next_request_id = limit" in settings
        assert "muted/acknowledged 只停止本地蜂鸣器，不改写 VC1 传感器真值" in header

    def durable_ids_only() -> None:
        try:
            IdAllocator(None)
        except OSError:
            pass
        else:
            raise AssertionError("model allocated an ID without durable storage")

        safe, detail = durable_request_id_contract(sensor, settings)
        assert safe, detail
        assert not durable_request_id_contract(
            sensor + "\nvelacare_sensor_use_ephemeral_request_ids();\n",
            settings)[0], "ephemeral negative control was not rejected"

    def settings_io_fail_closed() -> None:
        def model_load(open_error: Optional[str], reads: Sequence[object],
                       valid: bool = True) -> str:
            if open_error is not None:
                if open_error == "ENOENT":
                    return "INITIALIZE"
                raise OSError(open_error)
            data = bytearray()
            for item in reads:
                if item == "EINTR":
                    continue
                if not isinstance(item, bytes):
                    raise TypeError("read event must be bytes or EINTR")
                if not item:
                    break
                data.extend(item)
            if len(data) != 20:
                raise ValueError("short or oversized settings file")
            if not valid:
                raise ValueError("damaged settings file")
            return "LOADED"

        assert model_load("ENOENT", ()) == "INITIALIZE"
        assert model_load(None, (b"a" * 7, "EINTR", b"b" * 13)) == "LOADED"
        for args in (("EACCES", (), True), (None, (b"x" * 19,), True),
                     (None, (b"x" * 20,), False)):
            try:
                model_load(*args)
            except (OSError, ValueError):
                pass
            else:
                raise AssertionError(f"fail-closed model accepted {args!r}")

        safe, detail = settings_fail_closed_contract(settings)
        assert safe, detail
        assert not settings_fail_closed_contract(
            settings.replace("EINTR", "INTERRUPT_DISABLED"))[0], (
                "EINTR negative control was not rejected")

    def history_capacity_static_and_model() -> None:
        safe, detail = family_history_capacity_contract(settings_h, gateway)
        assert safe, detail
        too_small = re.sub(
            r"(#define\s+VELACARE_REQUEST_FAMILY_HISTORY\s+)\d+",
            r"\g<1>8", settings_h)
        assert not family_history_capacity_contract(too_small, gateway)[0]

        id_nv: Dict[str, int] = {}
        state_nv: dict = {}
        d = D12X(IdAllocator(id_nv), state_nv, True)
        notifications: List[str] = []
        for index in range(FAMILY_NOTIFICATION_CAPACITY):
            d.send_sos()
            notification = make_frame(
                f"FAM1,{d.sos_id},0,{index % 4 + 1}")
            notifications.append(notification)
            assert d.receive(notification)
        assert len(d.fam_history) == FAMILY_NOTIFICATION_CAPACITY
        restarted = D12X(IdAllocator(id_nv), state_nv, True)
        assert restarted.receive(notifications[0]), (
            "oldest of ESP32's 12 queued replies was forgotten")

    def separate_sos_and_care_replies() -> None:
        id_nv: Dict[str, int] = {}
        state_nv: dict = {}
        d = D12X(IdAllocator(id_nv), state_nv, True)
        d.send_sos()
        sos_id = d.sos_id
        d.send_care(1)
        care_id = d.care_id
        assert d.receive(make_frame(f"FAM1,{sos_id},0,4"))
        assert d.sos_reply == 4 and d.care_reply == 0
        assert d.receive(make_frame(f"FAM1,{care_id},1,2"))
        assert d.sos_reply == 4 and d.care_reply == 2
        restarted = D12X(IdAllocator(id_nv), state_nv, True)
        assert (restarted.sos_reply, restarted.care_reply) == (4, 2)
        restarted.send_care(2)
        assert restarted.sos_reply == 4 and restarted.care_reply == 0

        safe, detail = separated_reply_contract(sources)
        assert safe, detail

    def sos_ui_specific_reply() -> None:
        safe, detail = sos_ui_reply_contract(sources)
        assert safe, detail
        mutant = dict(sources)
        mutant["D12X LVGL"] = mutant["D12X LVGL"].replace(
            "family_reply_text(", "family_reply_text_disabled(")
        assert not sos_ui_reply_contract(mutant)[0], (
            "fixed SOS text negative control was not rejected")

    def sos_and_stale_id() -> None:
        d, g = D12X(IdAllocator({})), Gateway({})
        for ack in g.receive(d.send_sos()):
            d.receive(ack)
        assert d.sos_status == "WAIT_FAMILY"
        assert not g.family_reply(0, d.sos_id + 1, 2)
        assert g.family_reply(0, d.sos_id, 3)
        fack = d.receive(g.pending()[0])
        assert len(fack) == 1
        g.receive(fack[0])
        assert d.sos_status == "FAMILY_SEEN" and not g.queue

    def cancel_flows() -> None:
        d, g = D12X(IdAllocator({})), Gateway({})
        for ack in g.receive(d.send_sos()):
            d.receive(ack)
        reply = g.receive(d.cancel_sos())[0]
        assert parse_frame(reply).endswith(",1")
        d.receive(reply)
        assert d.sos_status == "CANCELLED"
        for ack in g.receive(d.send_care(1)):
            d.receive(ack)
        reply = g.receive(d.cancel_care())[0]
        assert parse_frame(reply).endswith(",1,1")
        d.receive(reply)
        assert d.care_status == "CANCELLED"

    def care_types() -> None:
        for kind in (1, 2):
            d, g = D12X(IdAllocator({})), Gateway({})
            for ack in g.receive(d.send_care(kind)):
                d.receive(ack)
            assert d.care_status == "WAIT_FAMILY"
            assert g.family_reply(kind, d.care_id, 2 if kind == 1 else 1)
            fack = d.receive(g.pending()[0])[0]
            g.receive(fack)
            assert d.care_status == "FAMILY_SEEN" and not g.queue

    def wrong_ack_and_fam() -> None:
        d = D12X(IdAllocator({}))
        d.send_sos()
        d.receive(make_frame(f"SACK1,{d.sos_id + 1},2"))
        assert d.sos_status == "WAIT_GATEWAY"
        d2 = D12X(IdAllocator({}))
        d2.send_care(1)
        d2.receive(make_frame(f"CACK1,{d2.care_id},2,2"))
        assert d2.care_status == "WAIT_GATEWAY"
        assert not d2.receive(make_frame(f"FAM1,{d2.care_id + 1},1,2"))
        assert not d2.receive(make_frame(f"FAM1,{d2.care_id},2,2"))

    def duplicate_fam() -> None:
        d = D12X(IdAllocator({}))
        d.send_sos()
        notification = make_frame(f"FAM1,{d.sos_id},0,3")
        first, second = d.receive(notification), d.receive(notification)
        assert len(first) == len(second) == 1
        assert parse_frame(first[0]) == parse_frame(second[0]) == f"FACK1,{d.sos_id},0"

    def multiple_queue() -> None:
        g = Gateway({})
        for kind, rid in ((0, 101), (1, 102), (2, 103)):
            request = f"SOS1,{rid}" if kind == 0 else f"CARE1,{rid},{kind}"
            g.receive(make_frame(request))
            assert g.family_reply(kind, rid, kind + 1)
        g.receive(make_frame("FACK1,102,1"))
        assert g.queue == [(101, 0, 1), (103, 2, 3)]
        g.receive(make_frame("FACK1,999,0"))
        assert g.queue == [(101, 0, 1), (103, 2, 3)]

    def gateway_reboot() -> None:
        nv: dict = {}
        g = Gateway(nv)
        g.receive(make_frame("SOS1,501"))
        assert g.family_reply(0, 501, 3)
        g.receive(make_frame("CARE1,502,1"))
        restarted = Gateway(nv)
        assert restarted.queue == [(501, 0, 3)]
        assert restarted.history[(0, 501)] == "SEEN" and restarted.active[1] == 502

    def id_reboot() -> None:
        nv: Dict[str, int] = {}
        first = IdAllocator(nv)
        used = (first.take(), first.take())
        after = IdAllocator(nv).take()
        assert after not in used and after >= used[0] + 256

    def truth_isolation() -> None:
        d, g = D12X(IdAllocator({})), Gateway({})
        d.vc1(2, 2, 2, 2)
        g.sensors[:] = [2, 2, 2, 2]
        before_d, before_g = d.sensors[:], g.sensors[:]
        d.vcb1(1, 1, 3)
        d.confirm_safe()
        g.receive(d.send_care(2))
        g.muted = g.acknowledged = True
        g.receive(d.cancel_care())
        assert d.sensors == before_d and g.sensors == before_g

    def full_queue() -> None:
        g = Gateway({})
        for i in range(12):
            g.receive(make_frame(f"SOS1,{1000 + i}"))
            assert g.family_reply(0, 1000 + i, 1)
        snapshot = g.queue[:]
        g.receive(make_frame("SOS1,9999"))
        assert not g.family_reply(0, 9999, 1) and g.queue == snapshot

    def fam_state_persisted_before_fack() -> None:
        safe, detail = fam_persistence_contract(sensor)
        assert safe, detail

        # Negative controls guard the checker itself against the earlier false
        # positive: no commit, or an ACK placed before commit, must both fail.
        fam_anchor = sensor.index('if (strncmp(line, "FAM1,", 5) == 0)')
        prefix, fam_suffix = sensor[:fam_anchor], sensor[fam_anchor:]
        no_commit = prefix + fam_suffix.replace(
            "velacare_sensor_commit_request_state(",
            "velacare_sensor_commit_request_state_disabled(")
        assert not fam_persistence_contract(no_commit)[0]

        swapped = fam_suffix.replace(
            "velacare_sensor_commit_request_state(", "__FAM_COMMIT__(")
        swapped = swapped.replace(
            "velacare_sensor_write_family_ack(",
            "velacare_sensor_commit_request_state(")
        swapped = swapped.replace(
            "__FAM_COMMIT__(", "velacare_sensor_write_family_ack(")
        assert not fam_persistence_contract(prefix + swapped)[0]

    def d12x_reboot() -> None:
        assert persistent, "D12X runtime persistence not found: " + persistent_detail
        id_nv: Dict[str, int] = {}
        state_nv: dict = {}
        d, g = D12X(IdAllocator(id_nv), state_nv, persistent), Gateway({})
        g.receive(d.send_sos())
        assert g.family_reply(0, d.sos_id, 3)
        pending = g.pending()[0]
        assert d.receive(pending)
        restarted = D12X(IdAllocator(id_nv), state_nv, persistent)
        assert restarted.receive(pending), "persisted FAM history did not produce FACK after reboot"

    return (
        ("CRC vector, framing and bad-CRC rejection", crc_codec, False),
        ("Static frame/validation/persistence contracts", static_contract, False),
        ("Only durable request IDs; no ephemeral fallback", durable_ids_only, True),
        ("Settings ENOENT-only init and fail-closed reads",
         settings_io_fail_closed, True),
        ("D12X FAM history covers ESP32's 12-entry queue",
         history_capacity_static_and_model, True),
        ("SOS and CARE reply codes are separate and durable",
         separate_sos_and_care_replies, True),
        ("SOS UI renders the caregiver's specific reply",
         sos_ui_specific_reply, True),
        ("SOS, stale ID rejection and correct FAM close", sos_and_stale_id, False),
        ("SOS and care cancellation roundtrips", cancel_flows, False),
        ("Contact-me and wellbeing flows", care_types, False),
        ("Wrong ack/FAM ID and type rejected", wrong_ack_and_fam, False),
        ("Duplicate FAM produces duplicate FACK", duplicate_fam, False),
        ("Multiple replies and exact FACK deletion", multiple_queue, False),
        ("ESP32 reboot restores queue and activity", gateway_reboot, False),
        ("D12X request IDs not reused on reboot", id_reboot, False),
        ("Mute/confirm/cancel preserve sensor truth", truth_isolation, False),
        ("Full reply queue refuses overwrite", full_queue, False),
        ("FAM state persisted before FACK on SOS and care paths",
         fam_state_persisted_before_fack, True),
        ("D12X reboot restores request/FAM history", d12x_reboot, True),
    )


def run_all(items: Sequence[Tuple[str, Callable[[], None], bool]]) -> List[Result]:
    results: List[Result] = []
    for name, test, known_gap in items:
        try:
            test()
        except Exception as exc:
            result = Result(name, "FAIL", str(exc), known_gap)
        else:
            result = Result(name, "PASS", "contract satisfied", known_gap)
        results.append(result)
        print(f"{result.status:4}  {name}" + (f": {result.detail}" if result.status == "FAIL" else ""))
    return results


def digest(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(chunk)
    return value.hexdigest()


def write_report(results: Sequence[Result], sources: Dict[str, str]) -> None:
    passed = sum(item.status == "PASS" for item in results)
    failed = len(results) - passed
    capable, capability_detail = persistence_capability(sources)
    lines = [
        "# VelaCare D12X v0.8.0 / ESP32 v0.15.0 跨板协议审计",
        "",
        "- 审计日期：2026-09-18",
        "- 方法：业务源码只读静态检查 + 主机侧确定性状态机模拟",
        f"- 结果：**{len(results)} 项，PASS {passed}，FAIL {failed}**",
        f"- D12X 运行状态持久化识别：**{'是' if capable else '否'}**（`{capability_detail}`）",
        "",
        "## 协议矩阵",
        "",
        "| 方向 | 请求/通知 | 回执 | 校验 |",
        "|---|---|---|---|",
        "| D12X → ESP32 | `SOS1,<id>` | `SACK1,<id>,<status>` | id；status 1..2 |",
        "| D12X → ESP32 | `SOSX1,<id>` | `SXACK1,<id>,<status>` | 活动 id；status 0..2 |",
        "| D12X → ESP32 | `CARE1,<id>,<type>` | `CACK1,<id>,<type>,<status>` | id/type；status 1..2 |",
        "| D12X → ESP32 | `CAREX1,<id>,<type>` | `CXACK1,<id>,<type>,<status>` | 活动 id/type；status 0..2 |",
        "| ESP32 → D12X | `FAM1,<id>,<type>,<reply>` | `FACK1,<id>,<type>` | 精确 id/type；reply 1..4 |",
        "",
        "统一封装：`<payload>*<CRC16四位十六进制>\\n`。CRC16-CCITT-FALSE 初值 `0xFFFF`、多项式 `0x1021`，标准向量 `123456789 → 0x29B1`。",
        "",
        "## 用例结果",
        "",
        "| # | 结果 | 用例 | 说明 |",
        "|---:|:---:|---|---|",
    ]
    for index, item in enumerate(results, 1):
        detail = item.detail.replace("|", "\\|").replace("\n", " ")
        if item.known_gap and item.status == "FAIL":
            detail = "已知缺口：" + detail
        lines.append(f"| {index} | {item.status} | {item.name} | {detail} |")

    lines += ["", "## 源文件 SHA-256", "", "| 文件 | SHA-256 |", "|---|---|"]
    for name, path in PATHS.items():
        lines.append(f"| `{path.relative_to(ROOT).as_posix()}` ({name}) | `{digest(path)}` |")

    lines += [
        "",
        "## 重启恢复判定",
        "",
    ]
    if capable:
        lines += [
            "检测到 D12X 对 SOS/关怀/FAM 运行状态同时具有保存与加载接口；模拟重启后会恢复活动请求和已处理 FAM 键，并重新回复 FACK。",
        ]
    else:
        lines += [
            "当前源码未检测到 D12X 对 SOS/关怀/FAM 运行状态成对的保存、加载接口。`velacare_sensor_init()` 清空活动状态与 `g_family_ack_history`，因此 ESP32 持久 FAM 在 D12X 重启后可能无法被 FACK 清除。",
            "",
            "修复合入后直接重跑本脚本；只要保存/加载 API 覆盖请求与 FAM 历史，最后一项会由 FAIL 变为 PASS。",
        ]
    lines += [
        "",
        "## 重跑命令",
        "",
        "```powershell",
        "python .\\tmp\\protocol_simulation_v080\\test_velacare_protocol_v080.py",
        "python .\\tmp\\protocol_simulation_v080\\test_velacare_protocol_v080.py --strict",
        "```",
        "",
    ]
    REPORT.write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--strict", action="store_true", help="return nonzero for any FAIL")
    args = parser.parse_args()
    sources = load_sources()
    results = run_all(tests(sources))
    write_report(results, sources)
    passed = sum(item.status == "PASS" for item in results)
    failed = len(results) - passed
    print(f"\nSUMMARY total={len(results)} pass={passed} fail={failed}")
    print(f"REPORT  {REPORT}")
    return 1 if args.strict and failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
