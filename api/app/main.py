import asyncio
from collections.abc import AsyncGenerator
from contextlib import asynccontextmanager
from datetime import UTC, datetime

from app.config import get_settings
from app.db import create_database_tables, get_db_session
from app.influx_logger import log_auto_logout
from app.models.member import Member
from app.routes.health import router as health_router
from app.routes.members import router as members_router
from app.routes.pending_nfc import router as pending_nfc_router
from app.routes.registration import router as registration_router
from app.routes.scan_history import router as scan_history_router
from app.routes.ui_config import router as ui_config_router
from app.routes.users import router as users_router
from app.routes.web import router as web_router
from fastapi import FastAPI, Request
from fastapi.responses import RedirectResponse
from sqlalchemy import select

settings = get_settings()

_auto_logout_date: str | None = None


async def auto_logout_loop() -> None:
    """Check every minute and auto-logout all present members at 20:00 daily."""
    global _auto_logout_date
    while True:
        now = datetime.now(UTC)
        today_str = now.strftime("%Y-%m-%d")

        # At or past 20:00 UTC and haven't auto-logged out today
        # (TODO: make timezone-aware for local TZ if needed)
        if now.hour >= 20 and _auto_logout_date != today_str:
            try:
                from app.db import get_engine, get_session_factory
                from app.models.member import Member
                from sqlalchemy import select

                session_factory = get_session_factory()
                with session_factory() as db_session:
                    present_members = list(
                        db_session.scalars(
                            select(Member).where(Member.is_present.is_(True))
                        )
                    )
                    for member in present_members:
                        member.is_present = False
                        member.current_login_at = None
                    db_session.commit()
                    if present_members:
                        import logging

                        logging.info(
                            f"Auto-logout: logged out {len(present_members)} members at 20:00"
                        )
                        log_auto_logout(len(present_members))
            except Exception as exc:
                import logging

                logging.error(f"Auto-logout failed: {exc}")
            _auto_logout_date = today_str

        await asyncio.sleep(60)


@asynccontextmanager
async def lifespan(_: FastAPI) -> AsyncGenerator[None, None]:
    create_database_tables()

    # Start auto-logout background task
    task = asyncio.create_task(auto_logout_loop())

    yield

    task.cancel()


app = FastAPI(title=settings.app_name, lifespan=lifespan)

app.include_router(health_router)
app.include_router(health_router, prefix=settings.api_v1_prefix)
app.include_router(members_router, prefix=settings.api_v1_prefix)
app.include_router(pending_nfc_router, prefix=settings.api_v1_prefix)
app.include_router(ui_config_router, prefix=settings.api_v1_prefix)
app.include_router(users_router, prefix=settings.api_v1_prefix)
app.include_router(web_router, prefix=settings.api_v1_prefix)
app.include_router(scan_history_router, prefix=settings.api_v1_prefix)
app.include_router(ui_config_router, prefix=settings.api_v1_prefix)


# Redirect old /guests/ endpoints to /members/ for ESP32 compatibility
@app.middleware("http")
async def redirect_guests_to_members(request: Request, call_next):
    if request.url.path.startswith("/api/v1/guests/"):
        new_path = request.url.path.replace("/api/v1/guests/", "/api/v1/members/", 1)
        return RedirectResponse(url=new_path)
    return await call_next(request)
