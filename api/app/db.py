from collections.abc import Generator
from functools import lru_cache

from app.config import get_settings
from app.models import Base
from sqlalchemy import create_engine, text
from sqlalchemy.engine import Engine
from sqlalchemy.orm import Session, sessionmaker


@lru_cache
def get_engine() -> Engine:
    settings = get_settings()
    return create_engine(settings.sqlalchemy_database_url, pool_pre_ping=True)


@lru_cache
def get_session_factory() -> sessionmaker[Session]:
    return sessionmaker(bind=get_engine(), autoflush=False, autocommit=False)


def create_database_tables() -> None:
    engine = get_engine()
    # Create tables if they don't exist (idempotent – does not drop existing data)
    Base.metadata.create_all(bind=engine)


def get_db_session() -> Generator[Session, None, None]:
    session_factory = get_session_factory()
    with session_factory() as session:
        yield session
