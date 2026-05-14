/* ========================================================================== *
 *                        VITA NET NOTIFICATION DAEMON                        *
 * ========================================================================== */

#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/modulemgr.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/clib.h>
#include <psp2/net/net.h>
#include <psp2/sysmodule.h>
#include <psp2/rtc.h>

#include <taihen.h>
#include <string.h>
#include <stdint.h>

/* ========================================================================== *
 *                             MACRO E DEFINIZIONI                            *
 * ========================================================================== */

#define LOG(...) sceClibPrintf("[VITA_NET_DAEMON] " __VA_ARGS__)

// Macro per chiamare la funzione originale intercettata da taiHEN
#define TAI_CONTINUE_TYPED(func_type, ref, ...) \
  ( ((struct _tai_hook_user *)(ref))->next ? \
    ((func_type)((struct _tai_hook_user *)(((struct _tai_hook_user *)(ref))->next))->func)(__VA_ARGS__) : \
    ((func_type)((struct _tai_hook_user *)(ref))->old)(__VA_ARGS__) )

#define DEFAULT_APP_ID "NPXS10001"
#define DEFAULT_MSG_TYPE 140

/* ========================================================================== *
 *                             VARIABILI GLOBALI                              *
 * ========================================================================== */

// Puntatori a funzioni e strutture interne di SQLite
static void* stolen_app_db_handle = NULL;
static int (*sqlite3_exec)(void *db, const char *sql, int (*callback)(void*,int,char**,char**), void *arg, char **errmsg) = NULL;

// Riferimenti per l'hook di taiHEN
static tai_hook_ref_t true_prepare_hook_ref;
static SceUID true_prepare_hook_uid = -1;

/* ========================================================================== *
 *                                  HOOKS                                     *
 * ========================================================================== */

/**
 * Hook su sqlite3_prepare_v2 (NID: 0x93F666E9)
 * Scopo: Catturare l'handle del database delle notifiche (SceShell) al volo.
 */
static int true_sqlite3_prepare_v2_hook(void* db, const char* sql, int nByte, void** ppStmt, const char** pzTail) {
    if (stolen_app_db_handle == NULL) {
        // Verifica che la query si trovi in uno spazio di memoria sicuro (RAM user)
        if (sql != NULL && (uintptr_t)sql > 0x81000000 && (uintptr_t)sql < 0x8FFFFFFF) {
            
            // Appena SceShell interagisce con le tabelle delle notifiche, rubiamo l'handle
            if (strstr(sql, "tbl_") != NULL) {
                stolen_app_db_handle = db;
                LOG("\n[!!!] BINGO! HANDLE DB CATTURATO CON SUCCESSO! [!!!]\n");
                LOG("Query intercettata: %s\n", sql);
                LOG("Handle DB: 0x%08X\n", (uintptr_t)db);
                LOG("--------------------------------------------------\n\n");
            }
        }
    }
    
    // Passa l'esecuzione alla funzione originale
    return TAI_CONTINUE_TYPED(int (*)(void*, const char*, int, void**, const char**), 
                              true_prepare_hook_ref, db, sql, nByte, ppStmt, pzTail);
}

/* ========================================================================== *
 *                             LOGICA PRINCIPALE                              *
 * ========================================================================== */

/**
 * Inietta una nuova notifica nel database di SceShell tramite SQL Injection.
 */
void push_notification(const char* title_id, const char* message) {
    LOG("============= INIZIO INIEZIONE =============\n");

    if (stolen_app_db_handle && sqlite3_exec) {
        // Genera un ID univoco basato sul tick del rtc
        SceRtcTick tick;
        sceRtcGetCurrentTick(&tick);
        char unique_id[16];
        sceClibSnprintf(unique_id, sizeof(unique_id), "vn_%08x", (uint32_t)tick.tick);

        // Costruisci la query di inserimento
        char query[512];
        sceClibSnprintf(query, sizeof(query),
            "INSERT INTO tbl_newevent "
            "(id_title_id, id_item_id, msg_type, act_type, exec_mode, new_flag, del_flag, popup_no, title, exec_title_id, exec_arg) "
            "VALUES ('%s', '%s', %d, 60129542146, 131072, 1, 0, 1, '%s', '%s', 'type=USER_DEFINED');",
            title_id, unique_id, DEFAULT_MSG_TYPE, message, title_id);

        char *errmsg = NULL;
        int retries = 5;

        // Tenta l'esecuzione. Se il DB è occupato (SQLITE_BUSY), riprova.
        while (retries > 0) {
            int exec_res = sqlite3_exec(stolen_app_db_handle, query, NULL, NULL, &errmsg);
            if (exec_res == 0) { // SQLITE_OK
                LOG("[SQL] Notifica iniettata con successo! (AppID: %s)\n", title_id);
                break;
            } else if (exec_res == 5) { // SQLITE_BUSY
                sceKernelDelayThread(100 * 1000); // Attendi 100ms
                retries--;
            } else {
                LOG("[SQL] Errore %d - SQLite: %s\n", exec_res, errmsg ? errmsg : "Ignoto");
                break;
            }
        }
    }
    LOG("============= FINE INIEZIONE =============\n");
}

/**
 * Analizza il buffer ricevuto dal PC e avvia l'iniezione.
 * Si aspetta il formato: "TitleID|Messaggio"
 */
void parse_and_push(char* buffer) {
    char* first_pipe = strchr(buffer, '|');

    if (first_pipe != NULL) {
        *first_pipe = '\0'; // Divide la stringa
        char* target_id = buffer; 
        char* msg_text = first_pipe + 1;
        
        // Se il target_id è vuoto per errore di formattazione, usa il default
        if (target_id[0] == '\0') {
            target_id = DEFAULT_APP_ID;
        }
        
        push_notification(target_id, msg_text);
    } else {
        // Fallback: Se non c'è pipe, tratta tutto come messaggio e usa l'app di default
        push_notification(DEFAULT_APP_ID, buffer);
    }
}

/* ========================================================================== *
 *                             THREAD DI RETE E CORE                          *
 * ========================================================================== */

int network_thread(SceSize args, void *argp) {
    LOG("[VITA_NET] Boot... Attesa caricamento librerie...\n");
    sceKernelDelayThread(3 * 1000 * 1000); // Attesa di 3 secondi per sicurezza

    // 1. Risoluzione della funzione sqlite3_exec da SceSqliteVsh
    uintptr_t ptr = 0;
    if (taiGetModuleExportFunc("SceSqliteVsh", TAI_ANY_LIBRARY, 0x14D27083, &ptr) >= 0) {
        sqlite3_exec = (void*)ptr;
    }

    // 2. Registrazione dell'hook per rubare l'handle
    true_prepare_hook_uid = taiHookFunctionExport(&true_prepare_hook_ref, 
                                                  "SceSqliteVsh", 
                                                  TAI_ANY_LIBRARY, 
                                                  0x93F666E9, 
                                                  true_sqlite3_prepare_v2_hook);

    if (true_prepare_hook_uid < 0) {
        LOG("ERRORE: HOOK SU 0x93F666E9 FALLITO: 0x%08X\n", true_prepare_hook_uid);
    } else {
        LOG("HOOK ATTIVO! Muoviti nella Home per scatenare una query...\n");
    }

    // 3. Loop di attesa finché non otteniamo l'handle del DB
    while (!stolen_app_db_handle) {
        sceKernelDelayThread(500 * 1000);
    }

    // 4. Handle ottenuto! Non abbiamo più bisogno dell'hook, lo rilasciamo.
    if (true_prepare_hook_uid >= 0) {
        taiHookRelease(true_prepare_hook_uid, true_prepare_hook_ref);
        true_prepare_hook_uid = -1;
        LOG("[VITA_NET] Hook rilasciato! Server TCP in avvio...\n");
    }

    // 5. Avvio del Server TCP
    struct SceNetSockaddrIn server_addr;
    server_addr.sin_family = SCE_NET_AF_INET; 
    server_addr.sin_port = sceNetHtons(4034);
    
    // NOTA: Sostituisci questo IP con quello del tuo PC se necessario.
    // In un'implementazione server vera e propria, la Vita dovrebbe fare bind() e listen(),
    // ma qui si sta comportando da client che si connette al server Python.
    sceNetInetPton(SCE_NET_AF_INET, "192.168.1.24", &server_addr.sin_addr);

    while (1) {
        int sock = sceNetSocket("vtcd_tcp", SCE_NET_AF_INET, SCE_NET_SOCK_STREAM, 0);
        if (sock >= 0) {
            // Tenta la connessione al server Python
            if (sceNetConnect(sock, (struct SceNetSockaddr *)&server_addr, sizeof(server_addr)) >= 0) {
                char buffer[256]; 
                int recvd;
                
                // Ricezione continua finché la connessione è attiva
                while ((recvd = sceNetRecv(sock, buffer, sizeof(buffer)-1, 0)) > 0) {
                    buffer[recvd] = '\0';
                    parse_and_push(buffer);
                }
            }
            sceNetSocketClose(sock);
        }
        // Attendi 5 secondi prima di riprovare a connetterti se cade la linea
        sceKernelDelayThread(5 * 1000 * 1000);
    }
    return 0;
}

/* ========================================================================== *
 *                         ENTRY POINT DEL MODULO                             *
 * ========================================================================== */
int module_start(SceSize argc, const void *args) {
    SceUID thid = sceKernelCreateThread("VTCD_Thread", network_thread, 0x40, 0x4000, 0, 0, NULL);
    if (thid >= 0) {
        sceKernelStartThread(thid, 0, NULL);
    }
    return SCE_KERNEL_START_SUCCESS;
}

int module_stop(SceSize argc, const void *args) { 
    // Qui andrebbe idealmente inserita la logica di chiusura thread, se necessario.
    return SCE_KERNEL_STOP_SUCCESS; 
}
