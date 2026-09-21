#!/usr/bin/env python3
"""Explicit, bounded validation of the local LXY ESPHome Web REST interface.

Examples (run only after reviewing and authorizing the actual hardware test):
  python hardware_web.py --host 192.168.1.20 --mode read --output read.json
  python hardware_web.py --host 192.168.1.20 --mode exercise --output exercise.json

ESPHome 2026.9.0 uses URL-encoded ENTITY NAMES, not YAML IDs or legacy slugs.
`--prefix` prepends exact text to every entity name (normally empty).

HTTP 200 means the Web action was accepted/deferred, not acknowledged by the
charger. Exercise polls the separate configuration readback and explicit
transaction result. A Refresh obtains a fresh device query after staging.

No POST is retried (including redirects or Digest retry hooks). There is no
automatic restoration after an ambiguous outcome. Restorations in the normal
test sequence only run after the preceding change was fully verified.
"""

import argparse
from datetime import datetime, timezone
import json
import math
from pathlib import Path
import sys
import time
from urllib.parse import quote, urlsplit, urlunsplit

import requests
from requests.adapters import HTTPAdapter
from requests.auth import HTTPDigestAuth
import yaml


ENTITIES = {
    "output_voltage": ("sensor", "Output voltage"),
    "output_current": ("sensor", "Output current"),
    "telemetry_valid": ("binary_sensor", "Live output valid"),
    "voltage": ("sensor", "Readback set voltage"),
    "current": ("sensor", "Readback set current"),
    "ready": ("binary_sensor", "Charger ready"),
    "status": ("text_sensor", "Command status"),
    "target_voltage": ("number", "Target voltage"),
    "target_current": ("number", "Target current"),
    "apply": ("button", "Apply settings"),
    "refresh": ("button", "Refresh settings"),
    "ble": ("switch", "BLE connection"),
}
VERIFIED = "Verified: current configuration matches Apply"
READ_COMPLETE = "Configuration read; draft edits require explicit Apply"
DRAFT_UPDATED = "Draft updated; press Apply to send both values"


class StopTest(RuntimeError):
    pass


def timestamp():
    return datetime.now(timezone.utc).isoformat()


def finite_value(entity):
    value = entity.get("value")
    if isinstance(value, bool):
        return None
    try:
        result = float(value)
        return result if math.isfinite(result) else None
    except (TypeError, ValueError):
        return None


def near(actual, expected):
    return actual is not None and abs(actual - expected) < 0.025


class Driver:
    def __init__(self, args):
        self.args = args
        raw_host = args.host if "://" in args.host else "http://" + args.host
        parts = urlsplit(raw_host)
        if parts.scheme not in ("http", "https") or not parts.hostname or parts.username or parts.password:
            raise StopTest("--host must be a plain HTTP(S) hostname/IP, without credentials")
        if parts.path not in ("", "/") or parts.query or parts.fragment:
            raise StopTest("--host must not contain a path, query or fragment")
        self.base = urlunsplit((parts.scheme, parts.netloc, "", "", ""))
        credentials = yaml.safe_load(args.secrets.read_text())
        if not isinstance(credentials, dict) or not all(
            isinstance(credentials.get(key), str) and credentials[key]
            for key in ("web_username", "web_password")
        ):
            raise StopTest("Local secrets.yaml must define web_username and web_password")
        self.auth = HTTPDigestAuth(credentials["web_username"], credentials["web_password"])
        # No Session-wide auth: POST must not acquire HTTPDigestAuth's 401 retry
        # hook. GET establishes/refreshes the challenge; POST builds one header.
        self.session = requests.Session()
        self.session.trust_env = False
        self.session.mount("http://", HTTPAdapter(max_retries=0))
        self.session.mount("https://", HTTPAdapter(max_retries=0))
        self.started = time.monotonic()
        self.deadline = self.started + args.overall_timeout
        self.last_snapshot = None
        self.step = "initialization"
        self.report = {
            "started_at": timestamp(), "host": self.base, "mode": args.mode,
            "entity_name_prefix": args.prefix,
            "rest_semantics": "HTTP 200 is deferred-action acceptance, not charger confirmation",
            "post_retry_policy": "zero retries, no redirect, one precomputed Digest header",
            "on_error": "stop; do not restore settings or reconnect automatically",
            "status": "RUNNING", "completed_steps": [], "events": [],
        }
        self.save()

    def save(self):
        self.args.output.parent.mkdir(parents=True, exist_ok=True)
        temporary = self.args.output.with_suffix(self.args.output.suffix + ".tmp")
        temporary.write_text(json.dumps(self.report, indent=2, ensure_ascii=False, allow_nan=False) + "\n")
        temporary.replace(self.args.output)

    def event(self, kind, **details):
        self.report["events"].append({"time": timestamp(), "step": self.step, "kind": kind, **details})
        self.save()

    def check_time(self):
        if time.monotonic() >= self.deadline:
            raise StopTest("Overall time limit reached; no corrective writes attempted")

    def path(self, key, action=None):
        domain, name = ENTITIES[key]
        path = f"/{domain}/{quote(self.args.prefix + name, safe='')}"
        return path + ("/" + action if action else "")

    def get(self, key):
        self.check_time()
        path = self.path(key)
        self.event("request", method="GET", path=path)
        try:
            response = self.session.get(
                self.base + path, auth=self.auth,
                timeout=(self.args.http_timeout, self.args.http_timeout), allow_redirects=False,
            )
        except requests.RequestException as exc:
            self.event("transport_error", method="GET", path=path, error_type=type(exc).__name__)
            raise StopTest(f"GET transport failure at {path}; test stopped") from exc
        self.event("response", method="GET", path=path, http_status=response.status_code,
                   digest_challenge_statuses=[item.status_code for item in response.history])
        if response.status_code != 200:
            raise StopTest(f"GET {path} returned HTTP {response.status_code}; check exact entity names/prefix")
        try:
            data = response.json()
        except ValueError as exc:
            raise StopTest(f"GET {path} did not return JSON") from exc
        if not isinstance(data, dict):
            raise StopTest(f"GET {path} did not return an entity object")
        # Only these known state fields are logged; never headers or credentials.
        sanitized = {key: value for key, value in data.items() if key in ("id", "state", "value")}
        sanitized = json.loads(json.dumps(sanitized, default=str).replace(": NaN", ": null"))
        self.event("entity", entity=key, data=sanitized)
        return data

    def post_once(self, key, action, value=None):
        if self.args.mode != "exercise":
            raise StopTest("POST forbidden in read mode")
        self.check_time()
        # Authenticate a harmless fresh GET immediately before preparing the
        # one POST. This keeps the Digest nonce ready without mutating hardware.
        self.get("status")
        params = {"value": f"{value:.1f}"} if value is not None else None
        request = requests.Request("POST", self.base + self.path(key, action), params=params)
        prepared = self.session.prepare_request(request)
        try:
            authorization = self.auth.build_digest_header("POST", prepared.url)
        except (KeyError, AttributeError, ValueError) as exc:
            raise StopTest("No usable Digest challenge; refusing POST") from exc
        if not authorization:
            raise StopTest("No usable Digest challenge; refusing unauthenticated POST")
        prepared.headers["Authorization"] = authorization
        # There are intentionally no response hooks on this prepared request.
        if prepared.hooks.get("response"):
            raise StopTest("Unexpected POST response hook; refusing possible auth retry")
        path = urlsplit(prepared.url).path
        self.event("request", method="POST", path=path, parameters=params, attempt=1)
        try:
            response = self.session.send(
                prepared, timeout=(self.args.http_timeout, self.args.http_timeout), allow_redirects=False,
            )
        except requests.RequestException as exc:
            self.event("ambiguous_post", path=path, error_type=type(exc).__name__, retried=False)
            raise StopTest(f"POST {path} transport outcome unknown; no retry or restoration") from exc
        self.event("response", method="POST", path=path, http_status=response.status_code, retried=False)
        if response.status_code != 200:
            raise StopTest(f"POST {path} returned HTTP {response.status_code}; no retry or restoration")

    def snapshot(self):
        data = {key: self.get(key) for key in ("voltage", "current", "ready", "status", "target_voltage", "target_current", "ble")}
        result = {
            "voltage": finite_value(data["voltage"]), "current": finite_value(data["current"]),
            "ready": data["ready"].get("value") is True,
            "status": str(data["status"].get("value", data["status"].get("state", ""))),
            "target_voltage": finite_value(data["target_voltage"]),
            "target_current": finite_value(data["target_current"]),
            "ble_enabled": data["ble"].get("value") is True,
        }
        if self.args.telemetry:
            result["output_voltage"] = finite_value(self.get("output_voltage"))
            result["output_current"] = finite_value(self.get("output_current"))
            result["telemetry_valid"] = self.get("telemetry_valid").get("value") is True
        self.last_snapshot = result
        self.event("snapshot", state=result)
        return result

    def expect(self, predicate, description, *, duration=None, fail_on_status=True):
        self.step = description
        end = min(self.deadline, time.monotonic() + (duration or self.args.step_timeout))
        while time.monotonic() < end:
            state = self.snapshot()
            if fail_on_status and any(token in state["status"].lower() for token in (
                "error", "rejected", "failed", "mismatch", "timed out", "outcome unknown",
            )):
                raise StopTest(f"Device reported failure while {description}: {state['status']}")
            if predicate(state):
                self.report["completed_steps"].append({"time": timestamp(), "step": description, "state": state})
                self.save()
                return state
            self.check_time()
            time.sleep(min(self.args.poll_interval, max(0.0, end - time.monotonic())))
        raise StopTest(f"Timed out while {description}; no retry or restoration")

    @staticmethod
    def configuration(state, voltage, current):
        return state["ready"] and near(state["voltage"], voltage) and near(state["current"], current)

    def change(self, axis, target, before_v, before_a, after_v, after_a):
        self.step = f"stage {axis} {target:.1f}"
        self.post_once("target_" + axis, "set", target)
        self.expect(
            lambda s: self.configuration(s, before_v, before_a) and
                      near(s["target_" + axis], target) and s["status"] == DRAFT_UPDATED,
            f"verify staged {axis} {target:.1f} without changed cached readback",
        )
        self.step = f"fresh config query before applying {axis} {target:.1f}"
        self.post_once("refresh", "press")
        self.expect(
            lambda s: self.configuration(s, before_v, before_a) and s["status"] == READ_COMPLETE,
            f"verify fresh device readback unchanged before {axis} Apply",
        )
        # Recheck both draft fields so no other value is accidentally applied.
        if not (near(self.last_snapshot["target_voltage"], after_v) and near(self.last_snapshot["target_current"], after_a)):
            raise StopTest("Draft values no longer match the intended Apply; no settings sent")
        self.step = f"Apply {after_v:.1f} V / {after_a:.1f} A exactly once"
        self.post_once("apply", "press")
        self.expect(
            lambda s: self.configuration(s, after_v, after_a) and s["status"] == VERIFIED,
            f"verify independent readback {after_v:.1f} V / {after_a:.1f} A",
        )

    def run(self):
        if self.args.mode == "read":
            self.step = "read-only snapshot"
            for index in range(self.args.samples):
                self.snapshot()
                if index + 1 < self.args.samples:
                    time.sleep(self.args.poll_interval)
            self.report["status"] = "READ_COMPLETE"
            return
        self.expect(
            lambda s: self.configuration(s, 58.4, 5.1) and s["ble_enabled"] and
                      near(s["target_voltage"], 58.4) and near(s["target_current"], 5.1),
            "verify original 58.4 V / 5.1 A, matching drafts, and BLE ready",
        )
        self.change("current", 5.0, 58.4, 5.1, 58.4, 5.0)
        self.change("current", 5.1, 58.4, 5.0, 58.4, 5.1)
        self.change("voltage", 58.3, 58.4, 5.1, 58.3, 5.1)
        self.change("voltage", 58.4, 58.3, 5.1, 58.4, 5.1)
        self.step = "disable BLE connection exactly once"
        self.post_once("ble", "turn_off")
        self.expect(
            lambda s: not s["ready"] and not s["ble_enabled"] and s["voltage"] is None and s["current"] is None,
            "verify BLE disabled and readback invalidated", fail_on_status=False,
        )
        self.step = "enable BLE connection exactly once"
        self.post_once("ble", "turn_on")
        self.expect(
            lambda s: self.configuration(s, 58.4, 5.1) and s["ble_enabled"] and s["status"] == READ_COMPLETE,
            "verify reconnect reads original 58.4 V / 5.1 A without automatic Apply",
            duration=self.args.reconnect_timeout,
        )
        self.report["status"] = "EXERCISE_PASS"


def arguments():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", required=True)
    parser.add_argument("--mode", required=True, choices=("read", "exercise"))
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--prefix", default="", help="Exact optional prefix before each entity name, including a trailing space if needed")
    workspace = Path(__file__).resolve().parents[1]
    parser.add_argument("--secrets", type=Path, default=workspace / "secrets.yaml")
    parser.add_argument("--telemetry", action="store_true", help="Include optional v2.1 live-output entities")
    parser.add_argument("--samples", type=int, default=3, help="Read-only snapshot count")
    parser.add_argument("--http-timeout", type=float, default=3.0)
    parser.add_argument("--step-timeout", type=float, default=20.0)
    parser.add_argument("--reconnect-timeout", type=float, default=45.0)
    parser.add_argument("--overall-timeout", type=float, default=240.0)
    parser.add_argument("--poll-interval", type=float, default=0.5)
    args = parser.parse_args()
    if args.output.exists():
        parser.error("--output already exists; choose a new file to preserve earlier evidence")
    if args.samples < 1 or any(getattr(args, key) <= 0 for key in (
        "http_timeout", "step_timeout", "reconnect_timeout", "overall_timeout", "poll_interval",
    )):
        parser.error("All limits and sample counts must be positive")
    return args


def main():
    args = arguments()
    driver = None
    try:
        driver = Driver(args)
        driver.run()
    except (StopTest, KeyboardInterrupt, OSError, ValueError, yaml.YAMLError) as exc:
        if driver is not None:
            driver.report["status"] = "STOPPED"
            driver.report["stopped_step"] = driver.step
            driver.report["error"] = "User interrupted" if isinstance(exc, KeyboardInterrupt) else str(exc)
            driver.report["last_snapshot"] = driver.last_snapshot
        print(f"STOPPED: {type(exc).__name__}; see JSON if initialization completed", file=sys.stderr)
        return 1
    finally:
        # Deliberately no hardware cleanup: unknown outcomes must not trigger
        # a second Apply, an automatic restore, or an automatic reconnect.
        if driver is not None:
            driver.report["finished_at"] = timestamp()
            driver.report["elapsed_seconds"] = round(time.monotonic() - driver.started, 3)
            driver.save()
            driver.session.close()
    print(f"{driver.report['status']}: {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
