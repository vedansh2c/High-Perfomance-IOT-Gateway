FROM debian:bookworm-slim AS cpp-build
RUN apt-get update && apt-get install -y --no-install-recommends g++ cmake make libgtest-dev && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY CMakeLists.txt .
COPY gateway gateway
COPY simulator simulator
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j4 && ctest --test-dir build --output-on-failure

FROM python:3.12-slim-bookworm AS gateway
RUN useradd -u 10001 -m pulse && mkdir -p /run/pulsegate && chown pulse /run/pulsegate
COPY --from=cpp-build /src/build/gateway/pulse_gateway /usr/local/bin/
COPY scripts/healthcheck.py /usr/local/bin/healthcheck.py
USER pulse
EXPOSE 9000
ENTRYPOINT ["pulse_gateway"]
CMD ["--ipc", "/run/pulsegate/gateway.sock"]

FROM python:3.12-slim-bookworm AS backend
RUN useradd -u 10001 -m pulse
WORKDIR /app
COPY backend/requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt
COPY --from=cpp-build /src/build/simulator/pulse_simulator /usr/local/bin/
COPY backend/app app
USER pulse
ENV PYTHONUNBUFFERED=1
EXPOSE 8000
CMD ["uvicorn", "app.main:app", "--host", "0.0.0.0", "--port", "8000", "--workers", "1"]
