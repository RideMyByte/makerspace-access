from collections.abc import Generator
from functools import lru_cache

from app.config import get_settings
from app.models import Base
from sqlalchemy import create_engine, inspect, text
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
    inspector = inspect(engine)
    existing_tables = inspector.get_table_names()
    if not existing_tables:
        # Fresh database – create all tables
        Base.metadata.create_all(bind=engine)
    # If tables exist, do nothing – data stays untouched


def get_db_session() -> Generator[Session, None, None]:
    session_factory = get_session_factory()
    with session_factory() as session:
        yield session
