from functools import lru_cache
from typing import ClassVar

from pydantic import Field, computed_field
from pydantic_settings import BaseSettings, SettingsConfigDict


class Settings(BaseSettings):
    """Application settings loaded from environment variables."""

    model_config: ClassVar[SettingsConfigDict] = SettingsConfigDict(
        env_file=None, extra="ignore"
    )

    app_name: str = "Makerspace Access API"
    api_v1_prefix: str = "/api/v1"
    api_key: str | None = Field(default=None, alias="API_KEY")
    registration_api_key: str | None = Field(default=None, alias="REGISTRATION_API_KEY")
    web_api_key: str | None = Field(default=None, alias="WEB_API_KEY")
    logo_inverted: bool = Field(default=False, alias="LOGO_INVERTED")
    logo_url: str = Field(
        default="https://halle1wh.de/content/images/2023/04/cropped-RZ_Halle1_Logo_2018_schwarz-e1540384559756-2-150x150-1.png",
        alias="LOGO_URL",
    )
    headline: str = Field(default="MakerSpace Access", alias="HEADLINE")

    # InfluxDB logging
    influx_enabled: bool = Field(default=False, alias="INFLUX_ENABLED")
    influx_url: str = Field(default="http://localhost:8086", alias="INFLUX_URL")
    influx_token: str = Field(default="", alias="INFLUX_TOKEN")
    influx_org: str = Field(default="makerspace", alias="INFLUX_ORG")
    influx_bucket: str = Field(default="makerspace-access", alias="INFLUX_BUCKET")

    postgres_db: str = Field(default="makerspace_access", alias="POSTGRES_DB")
    postgres_user: str = Field(default="makerspace", alias="POSTGRES_USER")
    postgres_password: str = Field(default="change-me", alias="POSTGRES_PASSWORD")
    postgres_host: str = Field(default="postgres", alias="POSTGRES_HOST")
    postgres_port: int = Field(default=5432, alias="POSTGRES_PORT")
    database_url: str | None = Field(default=None, alias="DATABASE_URL")

    @computed_field  # type: ignore[prop-decorator]
    @property
    def sqlalchemy_database_url(self) -> str:
        if self.database_url:
            return self.database_url

        return (
            f"postgresql+psycopg://{self.postgres_user}:{self.postgres_password}"
            f"@{self.postgres_host}:{self.postgres_port}/{self.postgres_db}"
        )


@lru_cache
def get_settings() -> Settings:
    return Settings()
