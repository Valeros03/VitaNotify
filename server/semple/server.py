import socket
import threading
import dbus
from dbus.mainloop.glib import DBusGMainLoop
from gi.repository import GLib
import time 

# --- CONFIGURAZIONE ---
HOST = '0.0.0.0'  # Ascolta su tutte le interfacce di rete
PORT = 4034       # Deve coincidere con il SERVER_PORT del plugin C

# --- MAPPATURA APP -> TITLE ID PS VITA ---
# Aggiungi qui le applicazioni Linux e l'ID corrispondente su PS Vita
APP_ID_MAP = {
    "Discord": "VTCD00001",    # Esempio fittizio
    "Telegram Desktop": "NPXS10001",
    "Spotify": "PCSE00013"     # Esempio fittizio
}
# ----------------------

last_payload = ""
last_time = 0

active_client = None
client_lock = threading.Lock()

def send_to_vita(app_id, summary, body):
    global active_client, last_payload, last_time
    
    testo = f"{summary}: {body}".replace('\n', ' ').replace('|', '')
    if len(testo) > 230:
        testo = testo[:227] + "..."
        
    # Costruiamo il payload: AppID|Testo\0 (Il msg_type ora è hardcoded in C)
    payload = f"{app_id}|{testo}\0"
    
    current_time = time.time()
    if payload == last_payload and (current_time - last_time) < 1.0:
        print("[PC] Notifica doppia ignorata (Debounce).")
        return
        
    last_payload = payload
    last_time = current_time
    
    with client_lock:
        if active_client:
            try:
                active_client.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
                active_client.sendall(payload.encode('utf-8'))
                print(f"[VITA] Inviato: {payload}")
                
                time.sleep(0.1) 
            except Exception as e:
                print(f"[VITA] Errore di invio: {e}")
                active_client.close()
                active_client = None

def tcp_server_thread():
    global active_client
    
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1) 
    server.bind((HOST, PORT))
    server.listen(1)
    
    print(f"[TCP] Server in ascolto sulla porta {PORT}...")
    
    while True:
        try:
            conn, addr = server.accept()
            print(f"[TCP] PS Vita connessa da {addr}")
            
            with client_lock:
                if active_client:
                    active_client.close()
                active_client = conn
                
            while True:
                data = conn.recv(1024)
                if not data:
                    break
                    
        except Exception as e:
            print(f"[TCP] Errore di rete: {e}")
            
        finally:
            print("[TCP] PS Vita disconnessa. In attesa di una nuova connessione...")
            with client_lock:
                if active_client == conn:
                    active_client.close()
                    active_client = None

def dbus_notification_handler(bus, message):
    interface = message.get_interface()
    member = message.get_member()

    if interface == "org.freedesktop.Notifications" and member == "Notify":
        print("\n" + "="*50)
        print("[DEBUG D-BUS] >>> INTERCETTATA CHIAMATA NOTIFY! <<<")
        
        args = message.get_args_list()
        
        if len(args) >= 5:
            app_name = str(args[0])
            summary = str(args[3])
            body = str(args[4])
            
            print(f"[DEBUG D-BUS] Parsing effettuato -> App: '{app_name}', Titolo: '{summary}', Testo: '{body}'")
            
            if summary or body:
                # Controlla se l'app è nella mappa. Se non c'è, usa "NPXS10001" come default
                vita_app_id = APP_ID_MAP.get(app_name, "NPXS10001")
                
                print(f"[D-BUS] OK: Invio alla Vita la notifica da '{app_name}' associata all'ID '{vita_app_id}'")
                send_to_vita(vita_app_id, summary, body)
            else:
                print("[DEBUG D-BUS] SCARTATA: Il titolo e il testo sono completamente vuoti.")
        else:
            print(f"[DEBUG D-BUS] SCARTATA: Struttura anomala.")
        
        print("="*50 + "\n")
        
def start_dbus_listener():
    DBusGMainLoop(set_as_default=True)
    session_bus = dbus.SessionBus()
    
    bus_obj = session_bus.get_object('org.freedesktop.DBus', '/org/freedesktop/DBus')
    monitor = dbus.Interface(bus_obj, 'org.freedesktop.DBus.Monitoring')
    
    rule = "type='method_call',interface='org.freedesktop.Notifications',member='Notify'"
    
    try:
        monitor.BecomeMonitor([rule], dbus.UInt32(0))
        print("[D-BUS] Modalità Monitor attivata con successo.")
    except Exception as e:
        print(f"[D-BUS] Impossibile attivare il Monitor: {e}")
    
    session_bus.add_message_filter(dbus_notification_handler)
    
    print("[D-BUS] In ascolto per le notifiche di Linux...")
    loop = GLib.MainLoop()
    loop.run()

if __name__ == '__main__':
    server_thread = threading.Thread(target=tcp_server_thread, daemon=True)
    server_thread.start()
    
    try:
        start_dbus_listener()
    except KeyboardInterrupt:
        print("\nChiusura del server...")
        if active_client:
            active_client.close()
