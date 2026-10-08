import asyncio
import json
import os


async def command(text: str):
    async def exchange():
        reader, writer = await asyncio.open_unix_connection(
            os.getenv("GATEWAY_IPC", "/run/pulsegate/gateway.sock"), limit=262144
        )
        try:
            writer.write((text + "\n").encode())
            await writer.drain()
            response = json.loads(await reader.readline())
            if "error" in response:
                raise RuntimeError(response["error"])
            return response
        finally:
            writer.close()
            await writer.wait_closed()

    return await asyncio.wait_for(exchange(), timeout=3)
