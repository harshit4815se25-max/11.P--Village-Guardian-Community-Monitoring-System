import json
import time
import sqlite3
import threading
from datetime import datetime
from collections import deque

import paho.mqtt.client as mqtt
from rich.console import Console
from rich.live import Live
from rich.layout import Layout
from rich.panel import Panel
from rich.table import Table
from rich.text import Text
from rich.align import Align


BROKER = "localhost"
PORT = 1883
TOPIC = "villageguardian/sensors"

DATABASE = "village_guardian.db"
DATA_TIMEOUT = 10

console = Console()
lock = threading.Lock()

connected = False
last_update = None

data = {
    "temperature": None,
    "humidity": None,
    "light": None,
    "rain": None,
    "water": None,
    "motion": None,
    "emergency_button": None,
    "status": "WAITING"
}

events = deque(maxlen=10)


def add_event(message):
    timestamp = datetime.now().strftime("%H:%M:%S")
    events.appendleft(f"{timestamp} | {message}")


def setup_database():
    with sqlite3.connect(DATABASE) as db:
        db.execute("""
            CREATE TABLE IF NOT EXISTS readings (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                timestamp TEXT,
                temperature REAL,
                humidity REAL,
                light REAL,
                rain INTEGER,
                water INTEGER,
                motion INTEGER,
                emergency_button INTEGER,
                status TEXT
            )
        """)


def save_data(sensor):
    with sqlite3.connect(DATABASE) as db:
        db.execute("""
            INSERT INTO readings (
                timestamp, temperature, humidity, light,
                rain, water, motion, emergency_button, status
            )
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
        """, (
            datetime.now().isoformat(timespec="seconds"),
            sensor.get("temperature"),
            sensor.get("humidity"),
            sensor.get("light"),
            sensor.get("rain"),
            sensor.get("water"),
            int(bool(sensor.get("motion"))),
            int(bool(sensor.get("emergency_button"))),
            sensor.get("status")
        ))


def on_connect(client, userdata, flags, rc):
    global connected

    connected = (rc == 0)

    if connected:
        client.subscribe(TOPIC)
        add_event("Arduino MQTT connection established")
    else:
        add_event(f"MQTT connection failed: {rc}")


def on_disconnect(client, userdata, rc):
    global connected
    connected = False
    add_event("MQTT connection disconnected")


def on_message(client, userdata, message):
    global last_update

    try:
        received = json.loads(message.payload.decode())

        if not isinstance(received, dict):
            raise ValueError("Invalid JSON object")

        with lock:
            old_status = data["status"]
            data.update(received)
            current = data.copy()
            last_update = time.time()

        save_data(current)

        if current["status"] != old_status:
            add_event("Status changed to " + str(current["status"]))

        if received.get("emergency_button") is True:
            add_event("Emergency button pressed")

        if received.get("motion") is True:
            add_event("Motion detected")

    except Exception as error:
        add_event("Message error: " + str(error))


def sensor_table():
    with lock:
        current = data.copy()

    table = Table(expand=True, title="LIVE SENSOR READINGS")

    table.add_column("Sensor", style="cyan")
    table.add_column("Reading", justify="right")
    table.add_column("Unit")

    sensors = [
        ("Temperature", "temperature", "°C"),
        ("Humidity", "humidity", "%"),
        ("Light Intensity", "light", "lux"),
        ("Rain Sensor", "rain", "analog"),
        ("Water Level", "water", "analog")
    ]

    for label, key, unit in sensors:
        value = current[key]

        if value is None:
            display = "[yellow]Waiting...[/yellow]"
        else:
            display = f"[bold white]{value}[/bold white]"

        table.add_row(label, display, unit)

    return table


def safety_panel():
    with lock:
        current = data.copy()

    table = Table(expand=True, show_header=False)

    table.add_column("Safety Component")
    table.add_column("Current State", justify="right")

    motion = current["motion"]
    button = current["emergency_button"]

    if motion is None:
        motion_text = "[yellow]Waiting[/yellow]"
    else:
        motion_text = (
            "[red bold]DETECTED[/red bold]"
            if motion else "[green]NO MOTION[/green]"
        )

    if button is None:
        button_text = "[yellow]Waiting[/yellow]"
    else:
        button_text = (
            "[red bold]PRESSED[/red bold]"
            if button else "[green]NOT PRESSED[/green]"
        )

    table.add_row("PIR Motion", motion_text)
    table.add_row("Emergency Button", button_text)

    return Panel(table, title="COMMUNITY SAFETY", border_style="red")


def status_panel():
    with lock:
        status = str(data["status"]).upper()

    if status == "NORMAL":
        message = "[bold green]SYSTEM NORMAL[/bold green]"
        color = "green"

    elif status == "WARNING":
        message = "[bold yellow]WARNING - ATTENTION REQUIRED[/bold yellow]"
        color = "yellow"

    elif status == "EMERGENCY":
        message = "[bold white on red] EMERGENCY DETECTED [/bold white on red]"
        color = "red"

    else:
        message = "[cyan]WAITING FOR SENSOR DATA[/cyan]"
        color = "cyan"

    return Panel(
        Align.center(Text.from_markup(message)),
        title="SYSTEM STATUS",
        border_style=color
    )


def connection_panel():
    connection = (
        "[bold green]CONNECTED[/bold green]"
        if connected else "[bold red]DISCONNECTED[/bold red]"
    )

    if last_update is None:
        data_status = "[yellow]WAITING[/yellow]"
    elif time.time() - last_update > DATA_TIMEOUT:
        data_status = "[red]NO RECENT DATA[/red]"
    else:
        data_status = "[green]LIVE[/green]"

    table = Table(expand=True, show_header=False)

    table.add_column("Information")
    table.add_column("Value", justify="right")

    table.add_row("MQTT Broker", connection)
    table.add_row("Broker Address", BROKER)
    table.add_row("MQTT Port", str(PORT))
    table.add_row("Topic", TOPIC)
    table.add_row("Sensor Data", data_status)

    return Panel(table, title="CONNECTION MONITOR", border_style="blue")


def event_panel():
    table = Table(expand=True, show_header=False)
    table.add_column("Recent Events")

    if not events:
        table.add_row("No events recorded")

    for event in events:
        table.add_row(event)

    return Panel(table, title="SYSTEM ACTIVITY", border_style="magenta")


def create_dashboard():
    layout = Layout()

    layout.split_column(
        Layout(name="header", size=5),
        Layout(name="body"),
        Layout(name="footer", size=3)
    )

    layout["body"].split_row(
        Layout(name="left", ratio=3),
        Layout(name="right", ratio=2)
    )

    layout["left"].split_column(
        Layout(name="sensors", ratio=3),
        Layout(name="events", ratio=2)
    )

    layout["right"].split_column(
        Layout(name="connection", ratio=2),
        Layout(name="safety", ratio=2),
        Layout(name="status", ratio=1)
    )

    layout["header"].update(
        Panel(
            Align.center(
                Text(
                    "VILLAGE GUARDIAN\n"
                    "Community Emergency & Environmental Monitoring",
                    style="bold white"
                )
            ),
            border_style="bright_blue"
        )
    )

    layout["sensors"].update(sensor_table())
    layout["events"].update(event_panel())
    layout["connection"].update(connection_panel())
    layout["safety"].update(safety_panel())
    layout["status"].update(status_panel())

    layout["footer"].update(
        Panel(
            Align.center(
                Text(
                    "Raspberry Pi 4 | Arduino Nano 33 IoT | MQTT | SQLite",
                    style="cyan"
                )
            )
        )
    )

    return layout


def main():
    setup_database()

    client = mqtt.Client(client_id="VillageGuardianDashboard")

    client.on_connect = on_connect
    client.on_disconnect = on_disconnect
    client.on_message = on_message

    client.reconnect_delay_set(
        min_delay=1,
        max_delay=30
    )

    add_event("Village Guardian dashboard started")

    try:
        client.connect_async(BROKER, PORT, 60)
        client.loop_start()

        with Live(
            create_dashboard(),
            console=console,
            refresh_per_second=2,
            screen=True
        ) as live:

            while True:
                live.update(create_dashboard())
                time.sleep(0.5)

    except KeyboardInterrupt:
        add_event("Dashboard stopped")

    finally:
        client.loop_stop()
        client.disconnect()


if __name__ == "__main__":
    main()