from asyncio.queues import Queue
import json
import os
from aiohttp import web
import firebase_admin
from firebase_admin import credentials
from firebase_admin import messaging

CONFIG_FILE = "config.json"
DEFAULT_CONFIG = {
    "auto_alert": True
}

esp_queues = set()

cred = credentials.Certificate("firebase-admin.json")
firebase_admin.initialize_app(cred)

def read_config():
    if os.path.exists(CONFIG_FILE):
        with open(CONFIG_FILE) as fp:
            return json.load(fp)
    else:
        return DEFAULT_CONFIG

def read_from_config(*keys):
    data = read_config()
    if len(keys) == 1:
        return data.get(keys[0])
    else:
        return {x: data.get(x) for x in keys}

def save_to_config(**items):
    data = read_config()
    data.update(items)

    with open(CONFIG_FILE, "w") as fp:
        json.dump(data, fp)

async def mobile_api(request: web.Request):
    if request.method == "GET":
        return web.json_response(read_from_config("auto_alert", "use_lights"))
    elif request.method == "POST":
        data = await request.json()
        save_to_config(**data)
        print("putting update event")

        for esp_queue in esp_queues:
            if data["auto_alert"]:
                await esp_queue.put("autoalert")
            else:
                await esp_queue.put("noautoalert")

            if data["use_lights"]:
                await esp_queue.put("uselights")
            else:
                await esp_queue.put("nouselights")

        return web.Response()

async def notify(request: web.Request):
    print("Got notification")
    messaging.send(messaging.Message(
        dict(
            type="alert",
            auto_alert="enabled" if read_from_config("auto_alert") else "disabled",
        ),
        token=read_from_config("fcm_token")
    ))
    return web.Response()

async def alert(request: web.Request):
    print("Sending alert")
    for esp_queue in esp_queues:
        await esp_queue.put("alert")
    return web.Response()

async def dismiss(request: web.Request):
    print("Sending dismiss event")
    for esp_queue in esp_queues:
        await esp_queue.put("dismiss")
    return web.Response()

async def fcm_token(request: web.Request):
    token = await request.json()
    save_to_config(fcm_token=token)
    return web.Response()

async def send_initial_signals(ws: web.WebSocketResponse()):
    if read_from_config("auto_alert"):
        await ws.send_str("autoalert")
    else:
        await ws.send_str("noautoalert")

    if read_from_config("use_lights"):
        await ws.send_str("uselights")
    else:
        await ws.send_str("nouselights")

async def esp8266(request: web.Request):
    ws = web.WebSocketResponse()
    await ws.prepare(request)

    queue = Queue()
    esp_queues.add(queue)

    print("got a connection from esp8266")
    await send_initial_signals(ws)

    try:
        while not ws.closed:
            event = await queue.get()
            if not ws.closed:
                print(f"sending event to esp: {event}")
                await ws.send_str(event)
    except Exception as e:
        print(e)
    finally:
        try:
            esp_queues.remove(queue)
        except KeyError:
            pass
        del queue

    return ws

app = web.Application()
app.add_routes([
    # ESP8266
    web.get("/esp8266", esp8266),
    web.post("/notify", notify),
    
    # Android
    web.get("/mobile-api", mobile_api),
    web.post("/mobile-api", mobile_api),
    web.post("/fcm-token", fcm_token),
    web.post("/alert", alert),
    web.post("/dismiss", dismiss),
])

web.run_app(app, port=os.environ.get("PORT") or 8080)
