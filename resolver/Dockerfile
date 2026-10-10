FROM python:3.12-slim

ENV PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1 \
    JUKEBOX_DB=/data/resolver.db

RUN useradd --create-home --uid 10001 jukebox \
    && mkdir -p /data && chown jukebox:jukebox /data

WORKDIR /app
COPY pyproject.toml README.md ./
COPY jukebox_resolver ./jukebox_resolver
RUN pip install --no-cache-dir .

USER jukebox
VOLUME ["/data"]
EXPOSE 8787

CMD ["uvicorn", "jukebox_resolver.server:app", "--host", "0.0.0.0", "--port", "8787", "--no-access-log"]
