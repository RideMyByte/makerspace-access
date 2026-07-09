"""InfluxDB v2 HTTP logger for access events."""

from datetime import datetime, timezone
from functools import lru_cache
from typing import Any

import httpx
from app.config import get_settings


@lru_cache
def _should_log() -> bool:
    settings = get_settings()
    return settings.influx_enabled


@lru_cache
def _get_client() -> httpx.Client | None:
    settings = get_settings()
    if not settings.influx_enabled:
        return None
    return httpx.Client(
        base_url=settings.influx_url,
        headers={"Authorization": f"Token {settings.influx_token}"},
        timeout=5.0,
    )


def _write_point(
    measurement: str,
    tags: dict[str, str],
    fields: dict[str, Any],
    timestamp: datetime | None = None,
) -> None:
    """Write a single data point to InfluxDB v2 via the HTTP API."""
    if not _should_log():
        return

    client = _get_client()
    if client is None:
        return

    settings = get_settings()
    ts = timestamp or datetime.now(timezone.utc)
    ts_ns = int(ts.timestamp() * 1_000_000_000)

    # Build line protocol: measurement,tag=val field=val timestamp
    tag_str = ",".join(f"{k}={v}" for k, v in sorted(tags.items()))
    field_str = ",".join(f"{k}={v}" for k, v in sorted(fields.items()))

    line = f"{measurement},{tag_str} {field_str} {ts_ns}"

    try:
        response = client.post(
            "/api/v2/write",
            params={
                "org": settings.influx_org,
                "bucket": settings.influx_bucket,
            },
            content=line,
        )
        response.raise_for_status()
    except Exception as exc:
        import logging

        logging.getLogger(__name__).warning("Failed to write to InfluxDB: %s", exc)


def log_check_in(member_id: int, member_name: str, nfc_id: str) -> None:
    """Log a check-in (login) event."""
    _write_point(
        "access_event",
        tags={"member_id": str(member_id), "action": "check_in"},
        fields={
            "member_name": f'"{member_name}"',
            "nfc_id": f'"{nfc_id}"',
            "value": 1,
        },
    )


def log_check_out(
    member_id: int,
    member_name: str,
    nfc_id: str,
    duration_minutes: int,
) -> None:
    """Log a check-out (logout) event with session duration."""
    _write_point(
        "access_event",
        tags={"member_id": str(member_id), "action": "check_out"},
        fields={
            "member_name": f'"{member_name}"',
            "nfc_id": f'"{nfc_id}"',
            "duration_minutes": duration_minutes,
            "value": 1,
        },
    )


def log_auto_logout(member_count: int) -> None:
    """Log automatic daily logout of all present members."""
    _write_point(
        "access_event",
        tags={"action": "auto_logout"},
        fields={
            "member_count": member_count,
            "value": 1,
        },
    )


def log_safety_briefing(member_id: int, member_name: str) -> None:
    """Log a safety briefing update."""
    _write_point(
        "access_event",
        tags={"member_id": str(member_id), "action": "safety_briefing"},
        fields={
            "member_name": f'"{member_name}"',
            "value": 1,
        },
    )


def log_induction(member_id: int, member_name: str, induction_name: str) -> None:
    """Log an additional induction/instruction."""
    _write_point(
        "access_event",
        tags={"member_id": str(member_id), "action": "induction"},
        fields={
            "member_name": f'"{member_name}"',
            "induction_name": f'"{induction_name}"',
            "value": 1,
        },
    )
