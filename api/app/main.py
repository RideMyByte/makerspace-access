from collections.abc import AsyncGenerator
from contextlib import asynccontextmanager

from app.config import get_settings
from app.db import create_database_tables
from app.routes.health import router as health_router
from app.routes.members import router as members_router
from app.routes.pending_nfc import router as pending_nfc_router
from app.routes.registration import router as registration_router
from app.routes.scan_history import router as scan_history_router
from app.routes.users import router as users_router
from fastapi import FastAPI, Request
from fastapi.responses import RedirectResponse

settings = get_settings()


@asynccontextmanager
async def lifespan(_: FastAPI) -> AsyncGenerator[None, None]:
    create_database_tables()
    yield


app = FastAPI(title=settings.app_name, lifespan=lifespan)

app.include_router(health_router)
app.include_router(health_router, prefix=settings.api_v1_prefix)
app.include_router(members_router, prefix=settings.api_v1_prefix)
app.include_router(pending_nfc_router, prefix=settings.api_v1_prefix)
app.include_router(registration_router, prefix=settings.api_v1_prefix)
app.include_router(users_router, prefix=settings.api_v1_prefix)
app.include_router(scan_history_router, prefix=settings.api_v1_prefix)


# Redirect old /guests/ endpoints to /members/ for ESP32 compatibility
@app.middleware("http")
async def redirect_guests_to_members(request: Request, call_next):
    if request.url.path.startswith("/api/v1/guests/"):
        new_path = request.url.path.replace("/api/v1/guests/", "/api/v1/members/", 1)
        return RedirectResponse(url=new_path)
    return await call_next(request)
