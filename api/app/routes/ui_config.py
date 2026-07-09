from app.config import get_settings
from fastapi import APIRouter

router = APIRouter(tags=["ui-config"])


@router.get("/ui-config")
def ui_config():
    settings = get_settings()
    return {
        "logo_url": settings.logo_url,
        "logo_inverted": settings.logo_inverted,
        "headline": settings.headline,
        "app_name": settings.app_name,
    }
