#define _POSIX_C_SOURCE 200809L

#include "SecureNetwork.h"
#include "AuthService.h"
#include "SecureFunctions.h"
#include "ServerIdentity.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <poll.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

/* Kept here so this source also builds when older headers omit the new API. */
int DATASTORE_server_delete_content(const char *document_kind, const char *document_name, int *deleted, char *error,
                                    size_t error_size);
int SECURE_NETWORK_delete_document(const char *document_kind, const char *document_name, int *deleted, char *error,
                                   size_t error_size);
int SECURE_NETWORK_server_is_running(void);
int AUTH_SERVER_verify_password(const char *username, const char *password, const char *remote_ip, char *error,
                                size_t error_size);
int AUTH_DB_create_user(const char *username, const char *password, int enable_totp, int is_admin,
                        const unsigned char *totp_secret, const char *acting_admin, char *error, size_t error_size);
int AUTH_DB_reset_password(const char *username, const char *new_password, const char *acting_admin, char *error,
                           size_t error_size);
int AUTH_DB_set_totp(const char *username, const unsigned char *secret, const char *acting_admin, char *error,
                     size_t error_size);
int AUTH_DB_remove_totp(const char *username, const char *acting_admin, char *error, size_t error_size);
int AUTH_DB_set_role(const char *username, int role, const char *acting_admin, char *error, size_t error_size);
int AUTH_DB_delete_user(const char *username, const char *acting_admin, char *error, size_t error_size);

#if OPENSSL_VERSION_NUMBER < 0x30500000L
#error "SecureNetwork.c requires OpenSSL 3.5.0 or newer."
#endif

#define SECURE_NETWORK_MAGIC 0x52535051U
#define SECURE_NETWORK_VERSION 1U
#define SECURE_NETWORK_MAX_PAYLOAD (64U * 1024U * 1024U + 4096U)
#define SECURE_NETWORK_MAX_CLIENTS 32
#define SECURE_NETWORK_HANDSHAKES_PER_MINUTE 20
#define SECURE_NETWORK_EXPORTER_BYTES 64
#define SECURE_NETWORK_NONCE_BYTES 32
#define SECURE_NETWORK_HEADER_BYTES 16
#define SECURE_NETWORK_RESPONSE_FLAG 0x8000U
#define SECURE_NETWORK_TIMEOUT_SECONDS 10
#define SECURE_NETWORK_KEEPALIVE_IDLE_SECONDS 10
#define SECURE_NETWORK_KEEPALIVE_INTERVAL_SECONDS 2
#define SECURE_NETWORK_KEEPALIVE_PROBES 4
#define SECURE_NETWORK_TCP_USER_TIMEOUT_MS 20000

#define SECURE_NETWORK_TYPE_AUTH 1U
#define SECURE_NETWORK_TYPE_SAVE 2U
#define SECURE_NETWORK_TYPE_LOAD 3U
#define SECURE_NETWORK_TYPE_LIST 4U
#define SECURE_NETWORK_TYPE_LOGOUT 5U
#define SECURE_NETWORK_TYPE_USER_LIST 6U
#define SECURE_NETWORK_TYPE_DELETE 7U
#define SECURE_NETWORK_TYPE_USER_ADMIN 8U

#define SECURE_NETWORK_USER_ADMIN_CREATE 1U
#define SECURE_NETWORK_USER_ADMIN_RESET_PASSWORD 2U
#define SECURE_NETWORK_USER_ADMIN_SET_TOTP 3U
#define SECURE_NETWORK_USER_ADMIN_REMOVE_TOTP 4U
#define SECURE_NETWORK_USER_ADMIN_SET_ROLE 5U
#define SECURE_NETWORK_USER_ADMIN_DELETE 6U
#define SECURE_NETWORK_USER_ADMIN_HEADER_BYTES 12U
#define SECURE_NETWORK_USER_ADMIN_ENABLE_TOTP 0x0001U

#define SECURE_NETWORK_AUTH_MODE_LOGIN 0U
#define SECURE_NETWORK_AUTH_MODE_REAUTH 1U

#define SECURE_NETWORK_STATUS_ERROR 0U
#define SECURE_NETWORK_STATUS_OK 1U
#define SECURE_NETWORK_STATUS_TOTP_REQUIRED 2U

static const unsigned char Secure_Network_Proof_Domain[] = "RetroSpectrum ML-DSA-87 TLS channel proof v1";
static const char Secure_Network_Exporter_Label[] = "EXPORTER-RetroSpectrum-PQ-Identity-v1";

static SSL_CTX *Global_Secure_Server_Context = NULL;
static int Global_Secure_Listen_Fd = -1;
static pthread_t Global_Secure_Server_Thread;
static int Global_Secure_Server_Thread_Started = 0;
static volatile int Global_Secure_Server_Running = 0;
static pthread_mutex_t Global_Secure_Server_Lock = PTHREAD_MUTEX_INITIALIZER;
static int Global_Secure_Active_Clients = 0;
static char Global_Secure_Status[256] = "Secure network transport is not running.";

static SSL_CTX *Global_Secure_Client_Context = NULL;
static SSL *Global_Secure_Client_SSL = NULL;
static int Global_Secure_Client_Fd = -1;
static int Global_Secure_Client_Authenticated = 0;
static int Global_Secure_Client_Connection_Lost = 0;
static pthread_mutex_t Global_Secure_Client_Lock = PTHREAD_MUTEX_INITIALIZER;
static uint32_t Global_Secure_Request_Id = 1;

struct Type_Secure_Rate_Entry {
    uint32_t address;
    time_t window_start;
    unsigned int count;
};
static struct Type_Secure_Rate_Entry Global_Secure_Rate_Entries[64];
static pthread_mutex_t Global_Secure_Rate_Lock = PTHREAD_MUTEX_INITIALIZER;

typedef struct Type_Secure_Client_Thread {
    int fd;
    struct sockaddr_in peer;
} Type_Secure_Client_Thread;

static void secure_network_set_error(char *error, size_t error_size, const char *message) {
    /*
        Purpose: Sets the secure network error message
        Returns: No value
    */

    if (error && error_size > 0) {

        (void)sec_strcpy(error, error_size, message ? message : "Secure network error.");

    }
}

static void secure_network_set_status(const char *message) {
    /*
        Purpose: Sets the secure network status message
        Returns: No value
    */

    pthread_mutex_lock(&Global_Secure_Server_Lock);
    (void)sec_strcpy(Global_Secure_Status, sizeof(Global_Secure_Status), message ? message : "");
    pthread_mutex_unlock(&Global_Secure_Server_Lock);
}

static void secure_network_store_u16(unsigned char *output, uint16_t value) {
    /*
        Purpose: Stores the 16-bit unsigned
        Returns: No value
    */

    value = htons(value);
    memcpy(output, &value, sizeof(value));
}

static void secure_network_store_u32(unsigned char *output, uint32_t value) {
    /*
        Purpose: Stores the 32-bit unsigned
        Returns: No value
    */

    value = htonl(value);
    memcpy(output, &value, sizeof(value));
}

static void secure_network_store_u64(unsigned char *output, uint64_t value) {
    /*
        Purpose: Stores the 64-bit unsigned
        Returns: No value
    */

    for (int i = 7; i >= 0; i--) {
        output[i] = (unsigned char)(value & 0xffU);
        value >>= 8;
    }
}

static uint16_t secure_network_load_u16(const unsigned char *input) {
    /*
        Purpose: Loads the 16-bit unsigned
        Returns: Success status
    */

    uint16_t value;
    memcpy(&value, input, sizeof(value));
    return ntohs(value);
}

static uint32_t secure_network_load_u32(const unsigned char *input) {
    /*
        Purpose: Loads the 32-bit unsigned
        Returns: Success status
    */

    uint32_t value;
    memcpy(&value, input, sizeof(value));
    return ntohl(value);
}

static uint64_t secure_network_load_u64(const unsigned char *input) {
    /*
        Purpose: Loads the 64-bit unsigned
        Returns: Success status
    */

    uint64_t value = 0;
    for (int i = 0; i < 8; i++) {
        value = (value << 8) | input[i];
    }
    return value;
}

static int secure_network_ssl_write_all(SSL *ssl, const void *data, size_t size) {
    /*
        Purpose: Writes all requested TLS bytes
        Returns: Success status
    */

    const unsigned char *bytes = data;
    size_t offset = 0;

    while (offset < size) {
        size_t written = 0;

        if (SSL_write_ex(ssl, bytes + offset, size - offset, &written) != 1 || written == 0) {

            return 0;

        }
        offset += written;
    }
    return 1;
}

static int secure_network_ssl_read_all(SSL *ssl, void *data, size_t size) {
    /*
        Purpose: Reads all requested TLS bytes
        Returns: Success status
    */

    unsigned char *bytes = data;
    size_t offset = 0;

    while (offset < size) {
        size_t received = 0;

        if (SSL_read_ex(ssl, bytes + offset, size - offset, &received) != 1 || received == 0) {

            return 0;

        }
        offset += received;
    }
    return 1;
}

static int secure_network_send_frame(SSL *ssl, uint16_t type, uint32_t request_id, const void *payload,
                                     size_t payload_size) {
    /*
        Purpose: Sends the frame
        Returns: Success status
    */

    unsigned char header[SECURE_NETWORK_HEADER_BYTES];

    if (!ssl || payload_size > SECURE_NETWORK_MAX_PAYLOAD || (payload_size > 0 && !payload)) {

        return 0;

    }
    secure_network_store_u32(header, SECURE_NETWORK_MAGIC);
    secure_network_store_u16(header + 4, SECURE_NETWORK_VERSION);
    secure_network_store_u16(header + 6, type);
    secure_network_store_u32(header + 8, request_id);
    secure_network_store_u32(header + 12, (uint32_t)payload_size);
    return secure_network_ssl_write_all(ssl, header, sizeof(header)) &&
           (payload_size == 0 || secure_network_ssl_write_all(ssl, payload, payload_size));
}

static int secure_network_receive_frame(SSL *ssl, uint16_t *type, uint32_t *request_id, unsigned char **payload,
                                        size_t *payload_size) {
    /*
        Purpose: Receives the frame
        Returns: Success status
    */

    unsigned char header[SECURE_NETWORK_HEADER_BYTES];
    uint32_t length;
    unsigned char *buffer = NULL;

    if (!ssl || !type || !request_id || !payload || !payload_size ||
        !secure_network_ssl_read_all(ssl, header, sizeof(header)) ||
        secure_network_load_u32(header) != SECURE_NETWORK_MAGIC ||
        secure_network_load_u16(header + 4) != SECURE_NETWORK_VERSION) {

        return 0;

    }

    *type = secure_network_load_u16(header + 6);
    *request_id = secure_network_load_u32(header + 8);
    length = secure_network_load_u32(header + 12);

    if (length > SECURE_NETWORK_MAX_PAYLOAD) {

        return 0;

    }

    if (length > 0) {

        buffer = OPENSSL_malloc(length);

        if (!buffer || !secure_network_ssl_read_all(ssl, buffer, length)) {

            OPENSSL_clear_free(buffer, length);
            return 0;

        }

    }
    *payload = buffer;
    *payload_size = length;
    return 1;
}

static EVP_PKEY *secure_network_generate_tls_key(void) {
    /*
        Purpose: Generates a temporary TLS private key
        Returns: Result pointer
    */

    EVP_PKEY_CTX *context = EVP_PKEY_CTX_new_from_name(NULL, "EC", NULL);
    EVP_PKEY *key = NULL;

    if (!context || EVP_PKEY_keygen_init(context) <= 0 || EVP_PKEY_CTX_set_group_name(context, "secp384r1") <= 0 ||
        EVP_PKEY_generate(context, &key) <= 0) {

        EVP_PKEY_free(key);
        key = NULL;

    }
    EVP_PKEY_CTX_free(context);
    return key;
}

static X509 *secure_network_generate_tls_certificate(EVP_PKEY *key) {
    /*
        Purpose: Generates a temporary TLS certificate
        Returns: Result pointer
    */

    X509 *certificate = NULL;
    X509_NAME *name;
    unsigned int serial = 0;

    if (!key || RAND_bytes((unsigned char *)&serial, sizeof(serial)) != 1 || !(certificate = X509_new()) ||
        X509_set_version(certificate, 2) != 1 ||
        ASN1_INTEGER_set(X509_get_serialNumber(certificate), (long)(serial | 1U)) != 1 ||
        !X509_gmtime_adj(X509_getm_notBefore(certificate), -60) ||
        !X509_gmtime_adj(X509_getm_notAfter(certificate), 86400) || X509_set_pubkey(certificate, key) != 1) {

        X509_free(certificate);
        return NULL;

    }

    name = X509_get_subject_name(certificate);

    if (!name ||
        X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, (const unsigned char *)"RetroSpectrum Ephemeral TLS", -1,
                                   -1, 0) != 1 ||
        X509_set_issuer_name(certificate, name) != 1 || X509_sign(certificate, key, EVP_sha384()) <= 0) {

        X509_free(certificate);
        return NULL;

    }
    return certificate;
}

static int secure_network_configure_context(SSL_CTX *context) {
    /*
        Purpose: Configures the context
        Returns: Success status
    */

    if (!context || SSL_CTX_set_min_proto_version(context, TLS1_3_VERSION) != 1 ||
        SSL_CTX_set_max_proto_version(context, TLS1_3_VERSION) != 1 ||
        SSL_CTX_set1_groups_list(context, "SecP384r1MLKEM1024") != 1 ||
        SSL_CTX_set_ciphersuites(context, "TLS_AES_256_GCM_SHA384") != 1) {

        return 0;

    }

    SSL_CTX_set_security_level(context, 4);
    SSL_CTX_set_options(context, SSL_OP_NO_COMPRESSION | SSL_OP_NO_RENEGOTIATION | SSL_OP_NO_TICKET);
    SSL_CTX_set_session_cache_mode(context, SSL_SESS_CACHE_OFF);
    SSL_CTX_set_max_early_data(context, 0);
    return 1;
}

static SSL_CTX *secure_network_create_server_context(void) {
    /*
        Purpose: Creates the server context
        Returns: Result pointer
    */

    SSL_CTX *context = SSL_CTX_new(TLS_server_method());
    EVP_PKEY *key = NULL;
    X509 *certificate = NULL;

    if (!context || !secure_network_configure_context(context) || !(key = secure_network_generate_tls_key()) ||
        !(certificate = secure_network_generate_tls_certificate(key)) ||
        SSL_CTX_use_certificate(context, certificate) != 1 || SSL_CTX_use_PrivateKey(context, key) != 1 ||
        SSL_CTX_check_private_key(context) != 1) {

        SSL_CTX_free(context);
        context = NULL;

    }
    X509_free(certificate);
    EVP_PKEY_free(key);
    return context;
}

static SSL_CTX *secure_network_create_client_context(void) {
    /*
        Purpose: Creates the client context
        Returns: Result pointer
    */

    SSL_CTX *context = SSL_CTX_new(TLS_client_method());

    if (!context || !secure_network_configure_context(context)) {

        SSL_CTX_free(context);
        return NULL;

    }
    /* The ephemeral TLS certificate is intentionally not trusted. The imported
       ML-DSA-87 key authenticates a channel-bound proof before any credential
       or application data is sent. */
    SSL_CTX_set_verify(context, SSL_VERIFY_NONE, NULL);
    return context;
}

static int secure_network_validate_negotiated_tls(SSL *ssl) {
    /*
        Purpose: Validates the negotiated TLS
        Returns: Boolean status
    */

    const char *cipher;
    const char *group;

    if (!ssl || SSL_version(ssl) != TLS1_3_VERSION) {

        return 0;

    }
    cipher = SSL_get_cipher_name(ssl);
    group = SSL_get0_group_name(ssl);
    return cipher && group && strcmp(cipher, "TLS_AES_256_GCM_SHA384") == 0 &&
           strcmp(group, "SecP384r1MLKEM1024") == 0 && SSL_session_reused(ssl) == 0;
}

static int secure_network_export_binding(SSL *ssl, unsigned char output[SECURE_NETWORK_EXPORTER_BYTES]) {
    /*
        Purpose: Exports the binding
        Returns: Success status
    */

    return ssl && SSL_export_keying_material(ssl, output, SECURE_NETWORK_EXPORTER_BYTES, Secure_Network_Exporter_Label,
                                             sizeof(Secure_Network_Exporter_Label) - 1, NULL, 0, 0) == 1;
}

static size_t secure_network_build_proof_message(const unsigned char client_nonce[SECURE_NETWORK_NONCE_BYTES],
                                                 const unsigned char server_nonce[SECURE_NETWORK_NONCE_BYTES],
                                                 const unsigned char binding[SECURE_NETWORK_EXPORTER_BYTES],
                                                 unsigned char *message, size_t message_size) {
    /*
        Purpose: Builds the proof message
        Returns: Computed size
    */

    size_t domain_size = sizeof(Secure_Network_Proof_Domain) - 1;
    size_t required = domain_size + 2 * SECURE_NETWORK_NONCE_BYTES + SECURE_NETWORK_EXPORTER_BYTES + 2;
    size_t offset = 0;

    if (!client_nonce || !server_nonce || !binding || !message || message_size < required) {

        return 0;

    }
    memcpy(message + offset, Secure_Network_Proof_Domain, domain_size);
    offset += domain_size;
    secure_network_store_u16(message + offset, SECURE_NETWORK_VERSION);
    offset += 2;
    memcpy(message + offset, client_nonce, SECURE_NETWORK_NONCE_BYTES);
    offset += SECURE_NETWORK_NONCE_BYTES;
    memcpy(message + offset, server_nonce, SECURE_NETWORK_NONCE_BYTES);
    offset += SECURE_NETWORK_NONCE_BYTES;
    memcpy(message + offset, binding, SECURE_NETWORK_EXPORTER_BYTES);
    return required;
}

static int secure_network_server_identity_proof(SSL *ssl) {
    /*
        Purpose: Builds a server identity proof
        Returns: Success status
    */

    unsigned char client_nonce[SECURE_NETWORK_NONCE_BYTES];
    unsigned char server_nonce[SECURE_NETWORK_NONCE_BYTES];
    unsigned char binding[SECURE_NETWORK_EXPORTER_BYTES];
    unsigned char proof_message[256];
    unsigned char signature[SERVER_IDENTITY_SIGNATURE_BYTES];
    size_t proof_size;
    int success = 0;

    if (!secure_network_ssl_read_all(ssl, client_nonce, sizeof(client_nonce)) ||
        RAND_bytes(server_nonce, sizeof(server_nonce)) != 1 || !secure_network_export_binding(ssl, binding) ||
        !(proof_size = secure_network_build_proof_message(client_nonce, server_nonce, binding, proof_message,
                                                          sizeof(proof_message))) ||
        !SERVER_IDENTITY_sign_local(proof_message, proof_size, signature) ||
        !secure_network_ssl_write_all(ssl, server_nonce, sizeof(server_nonce)) ||
        !secure_network_ssl_write_all(ssl, signature, sizeof(signature))) {

        goto cleanup;

    }
    success = 1;

cleanup:
    OPENSSL_cleanse(client_nonce, sizeof(client_nonce));
    OPENSSL_cleanse(server_nonce, sizeof(server_nonce));
    OPENSSL_cleanse(binding, sizeof(binding));
    OPENSSL_cleanse(proof_message, sizeof(proof_message));
    OPENSSL_cleanse(signature, sizeof(signature));
    return success;
}

static int secure_network_client_identity_proof(SSL *ssl, char *error, size_t error_size) {
    /*
        Purpose: Verifies a server identity proof
        Returns: Success status
    */

    unsigned char client_nonce[SECURE_NETWORK_NONCE_BYTES];
    unsigned char server_nonce[SECURE_NETWORK_NONCE_BYTES];
    unsigned char binding[SECURE_NETWORK_EXPORTER_BYTES];
    unsigned char proof_message[256];
    unsigned char signature[SERVER_IDENTITY_SIGNATURE_BYTES];
    size_t proof_size;
    int success = 0;

    if (RAND_bytes(client_nonce, sizeof(client_nonce)) != 1 ||
        !secure_network_ssl_write_all(ssl, client_nonce, sizeof(client_nonce)) ||
        !secure_network_ssl_read_all(ssl, server_nonce, sizeof(server_nonce)) ||
        !secure_network_ssl_read_all(ssl, signature, sizeof(signature)) ||
        !secure_network_export_binding(ssl, binding) ||
        !(proof_size = secure_network_build_proof_message(client_nonce, server_nonce, binding, proof_message,
                                                          sizeof(proof_message))) ||
        !SERVER_IDENTITY_verify_trusted(proof_message, proof_size, signature)) {

        secure_network_set_error(error, error_size, "The server failed ML-DSA-87 channel-bound identity verification.");
        goto cleanup;

    }
    success = 1;

cleanup:
    OPENSSL_cleanse(client_nonce, sizeof(client_nonce));
    OPENSSL_cleanse(server_nonce, sizeof(server_nonce));
    OPENSSL_cleanse(binding, sizeof(binding));
    OPENSSL_cleanse(proof_message, sizeof(proof_message));
    OPENSSL_cleanse(signature, sizeof(signature));
    return success;
}

static int secure_network_handshake_allowed(uint32_t address) {
    /*
        Purpose: Checks whether a secure handshake is allowed
        Returns: Success status
    */

    time_t now = time(NULL);
    int allowed = 0;
    size_t selected = 0;
    time_t oldest = now;

    pthread_mutex_lock(&Global_Secure_Rate_Lock);
    for (size_t i = 0; i < sizeof(Global_Secure_Rate_Entries) / sizeof(Global_Secure_Rate_Entries[0]); i++) {
        struct Type_Secure_Rate_Entry *entry = &Global_Secure_Rate_Entries[i];

        if (entry->address == address) {

            if (now - entry->window_start >= 60) {

                entry->window_start = now;
                entry->count = 0;

            }

            if (entry->count < SECURE_NETWORK_HANDSHAKES_PER_MINUTE) {

                entry->count++;
                allowed = 1;

            }
            pthread_mutex_unlock(&Global_Secure_Rate_Lock);
            return allowed;

        }

        if (entry->address == 0) {

            selected = i;
            oldest = 0;
            break;

        }

        if (entry->window_start < oldest) {

            oldest = entry->window_start;
            selected = i;

        }
    }
    Global_Secure_Rate_Entries[selected].address = address;
    Global_Secure_Rate_Entries[selected].window_start = now;
    Global_Secure_Rate_Entries[selected].count = 1;
    pthread_mutex_unlock(&Global_Secure_Rate_Lock);
    return 1;
}

static int secure_network_send_status(SSL *ssl, uint16_t request_type, uint32_t request_id, uint32_t status,
                                      const char *message, const void *extra, size_t extra_size) {
    /*
        Purpose: Sends the status
        Returns: Success status
    */

    size_t message_size = message ? strlen(message) : 0;
    size_t payload_size = 8 + message_size + extra_size;
    unsigned char *payload;
    int result;

    if (message_size > 65535 || payload_size > SECURE_NETWORK_MAX_PAYLOAD) {

        return 0;

    }
    payload = OPENSSL_malloc(payload_size);

    if (!payload) {

        return 0;

    }
    secure_network_store_u32(payload, status);
    secure_network_store_u32(payload + 4, (uint32_t)message_size);

    if (message_size > 0) {

        if (!sec_memcpy(payload + 8, payload_size - 8, message, message_size)) {

            OPENSSL_clear_free(payload, payload_size);
            return 0;

        }

    }

    if (extra_size > 0) {

        if (!sec_memcpy(payload + 8 + message_size, payload_size - 8 - message_size, extra, extra_size)) {

            OPENSSL_clear_free(payload, payload_size);
            return 0;

        }

    }

    result =
        secure_network_send_frame(ssl, request_type | SECURE_NETWORK_RESPONSE_FLAG, request_id, payload, payload_size);
    OPENSSL_clear_free(payload, payload_size);
    return result;
}

static int secure_network_parse_auth(const unsigned char *payload, size_t payload_size, char **username,
                                     char **password, char **totp, uint16_t *auth_mode) {
    /*
        Purpose: Parses the authentication
        Returns: Success status
    */

    uint16_t username_size;
    uint16_t password_size;
    uint16_t totp_size;
    size_t required;
    size_t offset = 8;

    if (!payload || payload_size < 8 || !username || !password || !totp || !auth_mode) {

        return 0;

    }
    username_size = secure_network_load_u16(payload);
    password_size = secure_network_load_u16(payload + 2);
    totp_size = secure_network_load_u16(payload + 4);
    *auth_mode = secure_network_load_u16(payload + 6);
    required = 8U + username_size + password_size + totp_size;

    if (required != payload_size || username_size == 0 || username_size > 63 || password_size == 0 ||
        password_size > 127 || totp_size > 8) {

        return 0;

    }

    if (memchr(payload + offset, '\0', username_size) != NULL) {

        return 0;

    }

    if (memchr(payload + offset + username_size, '\0', password_size) != NULL) {

        return 0;

    }

    if (totp_size > 0 && memchr(payload + offset + username_size + password_size, '\0', totp_size) != NULL) {

        return 0;

    }

    *username = OPENSSL_zalloc((size_t)username_size + 1);
    *password = OPENSSL_zalloc((size_t)password_size + 1);
    *totp = OPENSSL_zalloc((size_t)totp_size + 1);

    if (!*username || !*password || !*totp) {

        return 0;

    }

    if (!sec_str_memcpy(*username, (size_t)username_size + 1U, (const char *)payload + offset, username_size)) {

        return 0;

    }

    offset += username_size;

    if (!sec_str_memcpy(*password, (size_t)password_size + 1U, (const char *)payload + offset, password_size)) {

        return 0;

    }

    offset += password_size;

    if (totp_size > 0) {

        if (!sec_str_memcpy(*totp, (size_t)totp_size + 1U, (const char *)payload + offset, totp_size)) {

            return 0;

        }

    }

    return 1;
}

static int secure_network_handle_auth(SSL *ssl, uint32_t request_id, const unsigned char *payload, size_t payload_size,
                                      const char *remote_ip, int *authenticated, int *is_admin,
                                      char *authenticated_username, size_t authenticated_username_size) {
    /*
        Purpose: Handles initial authentication and password re-verification
        Returns: Handling status
    */

    char *username = NULL;
    char *password = NULL;
    char *totp = NULL;
    char message[256] = "";
    uint16_t auth_mode = SECURE_NETWORK_AUTH_MODE_LOGIN;
    int admin = 0;
    int result;
    unsigned char extra[4];

    if (!secure_network_parse_auth(payload, payload_size, &username, &password, &totp, &auth_mode)) {

        secure_network_send_status(ssl, SECURE_NETWORK_TYPE_AUTH, request_id, SECURE_NETWORK_STATUS_ERROR,
                                   "Malformed authentication request.", NULL, 0);
        goto cleanup;

    }

    if (auth_mode == SECURE_NETWORK_AUTH_MODE_REAUTH) {

        if (!authenticated || !*authenticated || !authenticated_username || authenticated_username[0] == '\0') {

            secure_network_send_status(ssl, SECURE_NETWORK_TYPE_AUTH, request_id, SECURE_NETWORK_STATUS_ERROR,
                                       "Authentication is required.", NULL, 0);
            goto cleanup;

        }

        if (strcmp(username, authenticated_username) != 0 || (totp && totp[0] != '\0')) {

            secure_network_send_status(ssl, SECURE_NETWORK_TYPE_AUTH, request_id, SECURE_NETWORK_STATUS_ERROR,
                                       "Invalid password verification request.", NULL, 0);
            goto cleanup;

        }

        result = AUTH_SERVER_verify_password(authenticated_username, password, remote_ip, message, sizeof(message));
        secure_network_send_status(
            ssl, SECURE_NETWORK_TYPE_AUTH, request_id, result ? SECURE_NETWORK_STATUS_OK : SECURE_NETWORK_STATUS_ERROR,
            result ? "Password verified." : (message[0] ? message : "Invalid password."), NULL, 0);
        goto cleanup;

    }

    if (auth_mode != SECURE_NETWORK_AUTH_MODE_LOGIN) {

        secure_network_send_status(ssl, SECURE_NETWORK_TYPE_AUTH, request_id, SECURE_NETWORK_STATUS_ERROR,
                                   "Unsupported authentication mode.", NULL, 0);
        goto cleanup;

    }

    result = AUTH_SERVER_authenticate(username, password, totp, remote_ip, &admin, message, sizeof(message));

    if (result == AUTH_SERVER_RESULT_SUCCESS) {

        *authenticated = 1;
        *is_admin = admin;

        if (authenticated_username && authenticated_username_size > 0) {

            if (!sec_strcpy(authenticated_username, authenticated_username_size, username)) {

                *authenticated = 0;
                *is_admin = 0;

                secure_network_send_status(ssl, SECURE_NETWORK_TYPE_AUTH, request_id, SECURE_NETWORK_STATUS_ERROR,
                                           "Authenticated username exceeds the session buffer.", NULL, 0);

                goto cleanup;

            }

        }
        secure_network_store_u32(extra, (uint32_t)admin);
        secure_network_send_status(ssl, SECURE_NETWORK_TYPE_AUTH, request_id, SECURE_NETWORK_STATUS_OK,
                                   "Authenticated.", extra, sizeof(extra));

    }

    else if (result == AUTH_SERVER_RESULT_TOTP_REQUIRED) {

        secure_network_send_status(ssl, SECURE_NETWORK_TYPE_AUTH, request_id, SECURE_NETWORK_STATUS_TOTP_REQUIRED,
                                   "Authenticator code required.", NULL, 0);

    }

    else {

        secure_network_send_status(ssl, SECURE_NETWORK_TYPE_AUTH, request_id, SECURE_NETWORK_STATUS_ERROR,
                                   message[0] ? message : "Authentication failed.", NULL, 0);

    }

cleanup:

    if (username) {

        OPENSSL_clear_free(username, strlen(username) + 1U);

    }

    if (password) {

        OPENSSL_clear_free(password, strlen(password) + 1U);

    }

    if (totp) {

        OPENSSL_clear_free(totp, strlen(totp) + 1U);

    }
    return 1;
}

static int secure_network_handle_save(SSL *ssl, uint32_t request_id, const unsigned char *payload,
                                      size_t payload_size) {
    /*
        Purpose: Handles the save
        Returns: Handling status
    */

    uint16_t kind_size, name_size, case_size;
    uint32_t content_size;
    size_t offset = 12;
    size_t required;
    char kind[64], name[DATASTORE_DOCUMENT_NAME_MAX], case_number[DATASTORE_CASE_NUMBER_MAX];
    char error[256] = "";

    if (!payload || payload_size < 12) {

        return secure_network_send_status(ssl, SECURE_NETWORK_TYPE_SAVE, request_id, SECURE_NETWORK_STATUS_ERROR,
                                          "Malformed save request.", NULL, 0);

    }
    kind_size = secure_network_load_u16(payload);
    name_size = secure_network_load_u16(payload + 2);
    case_size = secure_network_load_u16(payload + 4);
    content_size = secure_network_load_u32(payload + 8);
    required = 12U + kind_size + name_size + case_size + content_size;

    if (required != payload_size || kind_size == 0 || kind_size >= sizeof(kind) || name_size == 0 ||
        name_size >= sizeof(name) || case_size >= sizeof(case_number)) {

        return secure_network_send_status(ssl, SECURE_NETWORK_TYPE_SAVE, request_id, SECURE_NETWORK_STATUS_ERROR,
                                          "Malformed save request.", NULL, 0);

    }

    if (!sec_str_memcpy(kind, sizeof(kind), (const char *)payload + offset, kind_size)) {

        return secure_network_send_status(ssl, SECURE_NETWORK_TYPE_SAVE, request_id, SECURE_NETWORK_STATUS_ERROR,
                                          "Malformed save request.", NULL, 0);

    }

    offset += kind_size;

    if (!sec_str_memcpy(name, sizeof(name), (const char *)payload + offset, name_size)) {

        return secure_network_send_status(ssl, SECURE_NETWORK_TYPE_SAVE, request_id, SECURE_NETWORK_STATUS_ERROR,
                                          "Malformed save request.", NULL, 0);

    }

    offset += name_size;

    if (!sec_str_memcpy(case_number, sizeof(case_number), (const char *)payload + offset, case_size)) {

        return secure_network_send_status(ssl, SECURE_NETWORK_TYPE_SAVE, request_id, SECURE_NETWORK_STATUS_ERROR,
                                          "Malformed save request.", NULL, 0);

    }

    offset += case_size;

    if (!DATASTORE_server_save_content(kind, name, case_number, payload + offset, content_size, error, sizeof(error))) {

        return secure_network_send_status(ssl, SECURE_NETWORK_TYPE_SAVE, request_id, SECURE_NETWORK_STATUS_ERROR, error,
                                          NULL, 0);

    }

    return secure_network_send_status(ssl, SECURE_NETWORK_TYPE_SAVE, request_id, SECURE_NETWORK_STATUS_OK, "Saved.",
                                      NULL, 0);
}

static int secure_network_handle_load(SSL *ssl, uint32_t request_id, const unsigned char *payload,
                                      size_t payload_size) {
    /*
        Purpose: Handles the load
        Returns: Handling status
    */

    uint16_t kind_size, name_size;
    char kind[64], name[DATASTORE_DOCUMENT_NAME_MAX];
    unsigned char *content = NULL;
    size_t content_size = 0;
    int found = 0;
    char error[256] = "";
    unsigned char *extra = NULL;
    size_t extra_size = 8;
    int result;

    if (!payload || payload_size < 4) {

        return secure_network_send_status(ssl, SECURE_NETWORK_TYPE_LOAD, request_id, SECURE_NETWORK_STATUS_ERROR,
                                          "Malformed load request.", NULL, 0);

    }
    kind_size = secure_network_load_u16(payload);
    name_size = secure_network_load_u16(payload + 2);

    if (4U + kind_size + name_size != payload_size || kind_size == 0 || kind_size >= sizeof(kind) || name_size == 0 ||
        name_size >= sizeof(name)) {

        return secure_network_send_status(ssl, SECURE_NETWORK_TYPE_LOAD, request_id, SECURE_NETWORK_STATUS_ERROR,
                                          "Malformed load request.", NULL, 0);

    }

    if (!sec_str_memcpy(kind, sizeof(kind), (const char *)payload + 4, kind_size) ||
        !sec_str_memcpy(name, sizeof(name), (const char *)payload + 4 + kind_size, name_size)) {

        return secure_network_send_status(ssl, SECURE_NETWORK_TYPE_LOAD, request_id, SECURE_NETWORK_STATUS_ERROR,
                                          "Malformed load request.", NULL, 0);

    }

    if (!DATASTORE_server_load_content(kind, name, &content, &content_size, &found, error, sizeof(error))) {

        return secure_network_send_status(ssl, SECURE_NETWORK_TYPE_LOAD, request_id, SECURE_NETWORK_STATUS_ERROR, error,
                                          NULL, 0);

    }
    extra_size += content_size;
    extra = OPENSSL_malloc(extra_size);

    if (!extra) {

        DATASTORE_free_content(content, content_size);
        return 0;

    }
    secure_network_store_u32(extra, (uint32_t)found);
    secure_network_store_u32(extra + 4, (uint32_t)content_size);

    if (content_size > 0) {

        if (!sec_memcpy(extra + 8, extra_size - 8, content, content_size)) {

            OPENSSL_clear_free(extra, extra_size);
            DATASTORE_free_content(content, content_size);
            return 0;

        }

    }
    result = secure_network_send_status(ssl, SECURE_NETWORK_TYPE_LOAD, request_id, SECURE_NETWORK_STATUS_OK, "Loaded.",
                                        extra, extra_size);
    OPENSSL_clear_free(extra, extra_size);
    DATASTORE_free_content(content, content_size);
    return result;
}

static int secure_network_handle_delete(SSL *ssl, uint32_t request_id, const unsigned char *payload,
                                        size_t payload_size) {
    /*
        Purpose: Handles the delete
        Returns: Handling status
    */

    uint16_t kind_size;
    uint16_t name_size;
    char kind[64];
    char name[DATASTORE_DOCUMENT_NAME_MAX];
    char error[256] = "";
    unsigned char extra[4];
    int deleted = 0;

    if (!payload || payload_size < 4) {

        return secure_network_send_status(ssl, SECURE_NETWORK_TYPE_DELETE, request_id, SECURE_NETWORK_STATUS_ERROR,
                                          "Malformed delete request.", NULL, 0);

    }

    kind_size = secure_network_load_u16(payload);
    name_size = secure_network_load_u16(payload + 2);

    if (4U + kind_size + name_size != payload_size || kind_size == 0 || kind_size >= sizeof(kind) || name_size == 0 ||
        name_size >= sizeof(name)) {

        return secure_network_send_status(ssl, SECURE_NETWORK_TYPE_DELETE, request_id, SECURE_NETWORK_STATUS_ERROR,
                                          "Malformed delete request.", NULL, 0);

    }

    if (!sec_str_memcpy(kind, sizeof(kind), (const char *)payload + 4, kind_size) ||
        !sec_str_memcpy(name, sizeof(name), (const char *)payload + 4 + kind_size, name_size)) {

        return secure_network_send_status(ssl, SECURE_NETWORK_TYPE_DELETE, request_id, SECURE_NETWORK_STATUS_ERROR,
                                          "Malformed delete request.", NULL, 0);

    }

    if (!DATASTORE_server_delete_content(kind, name, &deleted, error, sizeof(error))) {

        return secure_network_send_status(ssl, SECURE_NETWORK_TYPE_DELETE, request_id, SECURE_NETWORK_STATUS_ERROR,
                                          error, NULL, 0);

    }

    secure_network_store_u32(extra, (uint32_t)deleted);
    return secure_network_send_status(ssl, SECURE_NETWORK_TYPE_DELETE, request_id, SECURE_NETWORK_STATUS_OK,
                                      deleted ? "Deleted." : "Document was not found.", extra, sizeof(extra));
}

static int secure_network_handle_list(SSL *ssl, uint32_t request_id, const unsigned char *payload,
                                      size_t payload_size) {
    /*
        Purpose: Handles the list
        Returns: Handling status
    */

    uint16_t kind_size;
    char kind[64];
    Type_DataStore_Document_Summary documents[512];
    size_t count = 0;
    size_t extra_size = 4;
    unsigned char *extra;
    size_t offset = 4;
    char error[256] = "";
    int result;

    if (!payload || payload_size < 2) {

        return secure_network_send_status(ssl, SECURE_NETWORK_TYPE_LIST, request_id, SECURE_NETWORK_STATUS_ERROR,
                                          "Malformed list request.", NULL, 0);

    }
    kind_size = secure_network_load_u16(payload);

    if (2U + kind_size != payload_size || kind_size == 0 || kind_size >= sizeof(kind)) {

        return secure_network_send_status(ssl, SECURE_NETWORK_TYPE_LIST, request_id, SECURE_NETWORK_STATUS_ERROR,
                                          "Malformed list request.", NULL, 0);

    }

    if (!sec_str_memcpy(kind, sizeof(kind), (const char *)payload + 2, kind_size)) {

        return secure_network_send_status(ssl, SECURE_NETWORK_TYPE_LIST, request_id, SECURE_NETWORK_STATUS_ERROR,
                                          "Malformed list request.", NULL, 0);

    }

    if (!DATASTORE_server_list_documents(kind, documents, 512, &count, error, sizeof(error))) {

        return secure_network_send_status(ssl, SECURE_NETWORK_TYPE_LIST, request_id, SECURE_NETWORK_STATUS_ERROR, error,
                                          NULL, 0);

    }
    for (size_t i = 0; i < count; i++) {
        extra_size += 12 + strlen(documents[i].document_name) + strlen(documents[i].case_number);
    }
    extra = OPENSSL_malloc(extra_size);

    if (!extra) {

        return 0;

    }
    secure_network_store_u32(extra, (uint32_t)count);
    for (size_t i = 0; i < count; i++) {
        size_t name_size = strlen(documents[i].document_name);
        size_t case_size = strlen(documents[i].case_number);
        secure_network_store_u16(extra + offset, (uint16_t)name_size);
        secure_network_store_u16(extra + offset + 2, (uint16_t)case_size);
        secure_network_store_u64(extra + offset + 4, (uint64_t)documents[i].updated_at);
        offset += 12;
        memcpy(extra + offset, documents[i].document_name, name_size);
        offset += name_size;
        memcpy(extra + offset, documents[i].case_number, case_size);
        offset += case_size;
    }
    result = secure_network_send_status(ssl, SECURE_NETWORK_TYPE_LIST, request_id, SECURE_NETWORK_STATUS_OK, "Listed.",
                                        extra, extra_size);
    OPENSSL_clear_free(extra, extra_size);
    return result;
}

static int secure_network_handle_user_list(SSL *ssl, uint32_t request_id) {
    /*
        Purpose: Handles the user list
        Returns: Handling status
    */

    Type_Auth_User_Summary users[512];
    size_t count = 0;
    size_t extra_size = 4;
    size_t offset = 4;
    unsigned char *extra;
    char error[256] = "";
    int result;

    if (!AUTH_DB_server_list_users(users, 512, &count, error, sizeof(error))) {

        return secure_network_send_status(ssl, SECURE_NETWORK_TYPE_USER_LIST, request_id, SECURE_NETWORK_STATUS_ERROR,
                                          error, NULL, 0);

    }
    for (size_t i = 0; i < count; i++) {
        extra_size += 20 + strlen(users[i].username);
    }
    extra = OPENSSL_malloc(extra_size);

    if (!extra) {

        return 0;

    }
    secure_network_store_u32(extra, (uint32_t)count);
    for (size_t i = 0; i < count; i++) {
        size_t username_size = strlen(users[i].username);
        secure_network_store_u16(extra + offset, (uint16_t)username_size);
        secure_network_store_u16(extra + offset + 2, (uint16_t)users[i].role);
        secure_network_store_u32(extra + offset + 4, (uint32_t)users[i].is_admin);
        secure_network_store_u32(extra + offset + 8, (uint32_t)users[i].totp_enabled);
        secure_network_store_u64(extra + offset + 12, (uint64_t)users[i].created_at);
        offset += 20;
        memcpy(extra + offset, users[i].username, username_size);
        offset += username_size;
    }
    result = secure_network_send_status(ssl, SECURE_NETWORK_TYPE_USER_LIST, request_id, SECURE_NETWORK_STATUS_OK,
                                        "Users listed.", extra, extra_size);
    OPENSSL_clear_free(extra, extra_size);
    return result;
}

static int secure_network_handle_user_admin(SSL *ssl, uint32_t request_id, const unsigned char *payload,
                                            size_t payload_size, const char *authenticated_username) {
    /*
        Purpose: Performs a co-administrator account-management operation on the server
        Returns: Handling status
    */

    uint16_t action;
    uint16_t username_size;
    uint16_t password_size;
    uint16_t secret_size;
    uint16_t role;
    uint16_t flags;
    size_t offset = SECURE_NETWORK_USER_ADMIN_HEADER_BYTES;
    char username[AUTH_PUBLIC_USERNAME_MAX + 1];
    char password[128];
    unsigned char secret[AUTH_PUBLIC_TOTP_SECRET_BYTES];
    char error[256] = "";
    int success = 0;

    memset(username, 0, sizeof(username));
    memset(password, 0, sizeof(password));
    memset(secret, 0, sizeof(secret));

    if (!payload || payload_size < SECURE_NETWORK_USER_ADMIN_HEADER_BYTES || !authenticated_username ||
        authenticated_username[0] == '\0') {

        return secure_network_send_status(ssl, SECURE_NETWORK_TYPE_USER_ADMIN, request_id, SECURE_NETWORK_STATUS_ERROR,
                                          "Malformed account-management request.", NULL, 0);

    }

    action = secure_network_load_u16(payload);
    username_size = secure_network_load_u16(payload + 2);
    password_size = secure_network_load_u16(payload + 4);
    secret_size = secure_network_load_u16(payload + 6);
    role = secure_network_load_u16(payload + 8);
    flags = secure_network_load_u16(payload + 10);

    if (username_size == 0 || username_size > AUTH_PUBLIC_USERNAME_MAX || password_size >= sizeof(password) ||
        secret_size > sizeof(secret) || offset + username_size + password_size + secret_size != payload_size) {

        return secure_network_send_status(ssl, SECURE_NETWORK_TYPE_USER_ADMIN, request_id, SECURE_NETWORK_STATUS_ERROR,
                                          "Malformed account-management request.", NULL, 0);

    }

    if (!sec_str_memcpy(username, sizeof(username), (const char *)payload + offset, username_size)) {

        return secure_network_send_status(ssl, SECURE_NETWORK_TYPE_USER_ADMIN, request_id, SECURE_NETWORK_STATUS_ERROR,
                                          "Malformed account-management request.", NULL, 0);

    }

    offset += username_size;

    if (password_size > 0) {

        if (!sec_str_memcpy(password, sizeof(password), (const char *)payload + offset, password_size)) {

            OPENSSL_cleanse(password, sizeof(password));
            OPENSSL_cleanse(secret, sizeof(secret));

            return secure_network_send_status(ssl, SECURE_NETWORK_TYPE_USER_ADMIN, request_id,
                                              SECURE_NETWORK_STATUS_ERROR, "Malformed account-management request.",
                                              NULL, 0);

        }

        offset += password_size;

    }

    if (secret_size > 0) {

        if (!sec_memcpy(secret, sizeof(secret), payload + offset, secret_size)) {

            OPENSSL_cleanse(password, sizeof(password));
            OPENSSL_cleanse(secret, sizeof(secret));

            return secure_network_send_status(ssl, SECURE_NETWORK_TYPE_USER_ADMIN, request_id,
                                              SECURE_NETWORK_STATUS_ERROR, "Malformed account-management request.",
                                              NULL, 0);

        }

    }

    switch (action) {
    case SECURE_NETWORK_USER_ADMIN_CREATE:

        if (password_size == 0 || (flags & ~SECURE_NETWORK_USER_ADMIN_ENABLE_TOTP) != 0 ||
            (((flags & SECURE_NETWORK_USER_ADMIN_ENABLE_TOTP) != 0) !=
             (secret_size == AUTH_PUBLIC_TOTP_SECRET_BYTES))) {

            secure_network_set_error(error, sizeof(error), "Malformed user-creation request.");
            break;

        }
        success = AUTH_DB_create_user(username, password, (flags & SECURE_NETWORK_USER_ADMIN_ENABLE_TOTP) != 0, 0,
                                      (flags & SECURE_NETWORK_USER_ADMIN_ENABLE_TOTP) != 0 ? secret : NULL,
                                      authenticated_username, error, sizeof(error));
        break;

    case SECURE_NETWORK_USER_ADMIN_RESET_PASSWORD:

        if (password_size == 0 || secret_size != 0) {

            secure_network_set_error(error, sizeof(error), "Malformed password-reset request.");
            break;

        }
        success = AUTH_DB_reset_password(username, password, authenticated_username, error, sizeof(error));
        break;

    case SECURE_NETWORK_USER_ADMIN_SET_TOTP:

        if (password_size != 0 || secret_size != AUTH_PUBLIC_TOTP_SECRET_BYTES) {

            secure_network_set_error(error, sizeof(error), "Malformed 2FA-enrollment request.");
            break;

        }
        success = AUTH_DB_set_totp(username, secret, authenticated_username, error, sizeof(error));
        break;

    case SECURE_NETWORK_USER_ADMIN_REMOVE_TOTP:

        if (password_size != 0 || secret_size != 0) {

            secure_network_set_error(error, sizeof(error), "Malformed 2FA-removal request.");
            break;

        }
        success = AUTH_DB_remove_totp(username, authenticated_username, error, sizeof(error));
        break;

    case SECURE_NETWORK_USER_ADMIN_SET_ROLE:

        if (password_size != 0 || secret_size != 0 || (role != AUTH_ROLE_USER && role != AUTH_ROLE_CO_ADMIN)) {

            secure_network_set_error(error, sizeof(error), "Malformed role-update request.");
            break;

        }
        success = AUTH_DB_set_role(username, (int)role, authenticated_username, error, sizeof(error));
        break;

    case SECURE_NETWORK_USER_ADMIN_DELETE:

        if (password_size != 0 || secret_size != 0) {

            secure_network_set_error(error, sizeof(error), "Malformed account-deletion request.");
            break;

        }
        success = AUTH_DB_delete_user(username, authenticated_username, error, sizeof(error));
        break;

    default:
        secure_network_set_error(error, sizeof(error), "Unsupported account-management operation.");
        break;
    }

    OPENSSL_cleanse(password, sizeof(password));
    OPENSSL_cleanse(secret, sizeof(secret));

    return secure_network_send_status(ssl, SECURE_NETWORK_TYPE_USER_ADMIN, request_id,
                                      success ? SECURE_NETWORK_STATUS_OK : SECURE_NETWORK_STATUS_ERROR,
                                      success ? "Account-management operation completed."
                                              : (error[0] ? error : "Account-management operation failed."),
                                      NULL, 0);
}

static void *secure_network_client_thread(void *argument) {
    /*
        Purpose: Runs a secure network client thread
        Returns: Thread result
    */

    Type_Secure_Client_Thread *client = argument;
    SSL *ssl = NULL;
    char remote_ip[INET_ADDRSTRLEN] = "unknown";
    char authenticated_username[64] = "";
    int authenticated = 0;
    int is_admin = 0;

    inet_ntop(AF_INET, &client->peer.sin_addr, remote_ip, sizeof(remote_ip));
    ssl = SSL_new(Global_Secure_Server_Context);

    if (!ssl) {

        goto cleanup;

    }
    SSL_set_fd(ssl, client->fd);

    if (SSL_accept(ssl) != 1 || !secure_network_validate_negotiated_tls(ssl) ||
        !secure_network_server_identity_proof(ssl)) {

        goto cleanup;

    }

    while (Global_Secure_Server_Running) {
        uint16_t type;
        uint32_t request_id;
        unsigned char *payload = NULL;
        size_t payload_size = 0;
        int keep = 1;

        if (!secure_network_receive_frame(ssl, &type, &request_id, &payload, &payload_size)) {

            break;

        }

        if (type == SECURE_NETWORK_TYPE_AUTH) {

            int was_authenticated = authenticated;

            keep = secure_network_handle_auth(ssl, request_id, payload, payload_size, remote_ip, &authenticated,
                                              &is_admin, authenticated_username, sizeof(authenticated_username));

            /*
                Remove the handshake/login socket timeout after successful
                authentication so an idle authenticated session remains connected.
            */

            if (keep && !was_authenticated && authenticated) {

                struct timeval no_timeout = {0, 0};

                setsockopt(client->fd, SOL_SOCKET, SO_RCVTIMEO, &no_timeout, sizeof(no_timeout));

                setsockopt(client->fd, SOL_SOCKET, SO_SNDTIMEO, &no_timeout, sizeof(no_timeout));

            }

        }

        else if (!authenticated) {

            keep = secure_network_send_status(ssl, type, request_id, SECURE_NETWORK_STATUS_ERROR,
                                              "Authentication is required.", NULL, 0);

        }

        else if (type == SECURE_NETWORK_TYPE_SAVE) {

            keep = secure_network_handle_save(ssl, request_id, payload, payload_size);

        }

        else if (type == SECURE_NETWORK_TYPE_LOAD) {

            keep = secure_network_handle_load(ssl, request_id, payload, payload_size);

        }

        else if (type == SECURE_NETWORK_TYPE_LIST) {

            keep = secure_network_handle_list(ssl, request_id, payload, payload_size);

        }

        else if (type == SECURE_NETWORK_TYPE_DELETE) {

            keep = secure_network_handle_delete(ssl, request_id, payload, payload_size);

        }

        else if (type == SECURE_NETWORK_TYPE_USER_LIST && payload_size == 0) {

            keep = secure_network_handle_user_list(ssl, request_id);

        }

        else if (type == SECURE_NETWORK_TYPE_USER_ADMIN) {

            keep = is_admin ? secure_network_handle_user_admin(ssl, request_id, payload, payload_size,
                                                               authenticated_username)
                            : secure_network_send_status(ssl, type, request_id, SECURE_NETWORK_STATUS_ERROR,
                                                         "Co-administrator privileges are required.", NULL, 0);

        }

        else if (type == SECURE_NETWORK_TYPE_LOGOUT) {

            secure_network_send_status(ssl, type, request_id, SECURE_NETWORK_STATUS_OK, "Logged out.", NULL, 0);
            keep = 0;

        }

        else {

            keep = secure_network_send_status(ssl, type, request_id, SECURE_NETWORK_STATUS_ERROR,
                                              "Unsupported request type.", NULL, 0);

        }
        OPENSSL_clear_free(payload, payload_size);

        if (!keep) {

            break;

        }
    }

cleanup:

    OPENSSL_cleanse(authenticated_username, sizeof(authenticated_username));

    if (ssl) {

        SSL_shutdown(ssl);
        SSL_free(ssl);

    }
    close(client->fd);
    OPENSSL_clear_free(client, sizeof(*client));
    pthread_mutex_lock(&Global_Secure_Server_Lock);

    if (Global_Secure_Active_Clients > 0) {

        Global_Secure_Active_Clients--;

    }
    pthread_mutex_unlock(&Global_Secure_Server_Lock);
    return NULL;
}

static void *secure_network_server_thread(void *unused) {
    /*
        Purpose: Runs the secure network server thread
        Returns: Thread result
    */

    (void)unused;
    while (Global_Secure_Server_Running) {
        Type_Secure_Client_Thread *client;
        socklen_t peer_size;
        pthread_t thread;
        int fd;

        client = OPENSSL_zalloc(sizeof(*client));

        if (!client) {

            struct timespec delay = {0, 100000000L};
            nanosleep(&delay, NULL);
            continue;

        }
        peer_size = sizeof(client->peer);
        fd = accept(Global_Secure_Listen_Fd, (struct sockaddr *)&client->peer, &peer_size);

        if (fd < 0) {

            OPENSSL_free(client);

            if (errno == EINTR) {

                continue;

            }

            if (!Global_Secure_Server_Running) {

                break;

            }
            continue;

        }
        client->fd = fd;

        if (!secure_network_handshake_allowed(client->peer.sin_addr.s_addr)) {

            close(fd);
            OPENSSL_free(client);
            continue;

        }
        pthread_mutex_lock(&Global_Secure_Server_Lock);

        if (Global_Secure_Active_Clients >= SECURE_NETWORK_MAX_CLIENTS) {

            pthread_mutex_unlock(&Global_Secure_Server_Lock);
            close(fd);
            OPENSSL_free(client);
            continue;

        }
        Global_Secure_Active_Clients++;
        pthread_mutex_unlock(&Global_Secure_Server_Lock);

        {
            struct timeval timeout = {SECURE_NETWORK_TIMEOUT_SECONDS, 0};
            setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
            setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
        }

        if (pthread_create(&thread, NULL, secure_network_client_thread, client) != 0) {

            close(fd);
            OPENSSL_free(client);
            pthread_mutex_lock(&Global_Secure_Server_Lock);
            Global_Secure_Active_Clients--;
            pthread_mutex_unlock(&Global_Secure_Server_Lock);
            continue;

        }
        pthread_detach(thread);
    }
    return NULL;
}

int SECURE_NETWORK_start_server(char *error, size_t error_size) {
    /*
        Purpose: Starts the server
        Returns: Success status
    */

    struct sockaddr_in address;
    int enabled = 1;

    if (Global_Secure_Server_Thread_Started) {

        return 1;

    }

    if (error && error_size > 0) {

        error[0] = '\0';

    }

    Global_Secure_Server_Context = secure_network_create_server_context();

    if (!Global_Secure_Server_Context) {

        secure_network_set_error(error, error_size, "Unable to configure TLS 1.3 SecP384r1MLKEM1024 server context.");
        return 0;

    }
    Global_Secure_Listen_Fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);

    if (Global_Secure_Listen_Fd < 0) {

        goto failure;

    }
    setsockopt(Global_Secure_Listen_Fd, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled));
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(SECURE_NETWORK_PORT);
    address.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(Global_Secure_Listen_Fd, (struct sockaddr *)&address, sizeof(address)) != 0 ||
        listen(Global_Secure_Listen_Fd, 32) != 0) {

        goto failure;

    }

    Global_Secure_Server_Running = 1;

    if (pthread_create(&Global_Secure_Server_Thread, NULL, secure_network_server_thread, NULL) != 0) {

        Global_Secure_Server_Running = 0;
        goto failure;

    }
    Global_Secure_Server_Thread_Started = 1;
    secure_network_set_status("TLS 1.3 SecP384r1MLKEM1024 server listening on port 47742.");
    return 1;

failure:
    secure_network_set_error(error, error_size, "Unable to bind secure server port 47742.");

    if (Global_Secure_Listen_Fd >= 0) {

        close(Global_Secure_Listen_Fd);

    }
    Global_Secure_Listen_Fd = -1;
    SSL_CTX_free(Global_Secure_Server_Context);
    Global_Secure_Server_Context = NULL;
    return 0;
}

int SECURE_NETWORK_server_is_running(void) {
    /*
        Purpose: Reports whether the secure LAN server is actively listening
        Returns: Boolean status
    */

    int running;

    pthread_mutex_lock(&Global_Secure_Server_Lock);
    running = Global_Secure_Server_Running && Global_Secure_Server_Thread_Started && Global_Secure_Listen_Fd >= 0;
    pthread_mutex_unlock(&Global_Secure_Server_Lock);
    return running;
}

void SECURE_NETWORK_stop_server(void) {
    /*
        Purpose: Stops the server
        Returns: No value
    */

    SECURE_NETWORK_disconnect();
    Global_Secure_Server_Running = 0;

    if (Global_Secure_Listen_Fd >= 0) {

        shutdown(Global_Secure_Listen_Fd, SHUT_RDWR);
        close(Global_Secure_Listen_Fd);
        Global_Secure_Listen_Fd = -1;

    }

    if (Global_Secure_Server_Thread_Started) {

        pthread_join(Global_Secure_Server_Thread, NULL);
        Global_Secure_Server_Thread_Started = 0;

    }
    SSL_CTX_free(Global_Secure_Server_Context);
    Global_Secure_Server_Context = NULL;
    secure_network_set_status("Secure network transport stopped.");
}

static void secure_network_close_client_locked(void) {
    /*
        Purpose: Closes the client locked
        Returns: No value
    */

    if (Global_Secure_Client_SSL) {

        SSL_shutdown(Global_Secure_Client_SSL);
        SSL_free(Global_Secure_Client_SSL);
        Global_Secure_Client_SSL = NULL;

    }

    if (Global_Secure_Client_Fd >= 0) {

        close(Global_Secure_Client_Fd);
        Global_Secure_Client_Fd = -1;

    }
    SSL_CTX_free(Global_Secure_Client_Context);
    Global_Secure_Client_Context = NULL;
    Global_Secure_Client_Authenticated = 0;
}

static void secure_network_abort_client_locked(void) {
    /*
        Purpose: Immediately releases a failed authenticated client connection
        Returns: No value
    */

    int was_authenticated = Global_Secure_Client_Authenticated;

    if (Global_Secure_Client_SSL) {

        SSL_free(Global_Secure_Client_SSL);
        Global_Secure_Client_SSL = NULL;

    }

    if (Global_Secure_Client_Fd >= 0) {

        close(Global_Secure_Client_Fd);
        Global_Secure_Client_Fd = -1;

    }
    SSL_CTX_free(Global_Secure_Client_Context);
    Global_Secure_Client_Context = NULL;
    Global_Secure_Client_Authenticated = 0;

    if (was_authenticated) {

        Global_Secure_Client_Connection_Lost = 1;

    }
}

static void secure_network_configure_client_keepalive(int fd) {
    /*
        Purpose: Configures fast TCP failure detection for the remote session
        Returns: No value
    */

    int enabled = 1;
    int idle_seconds = SECURE_NETWORK_KEEPALIVE_IDLE_SECONDS;
    int interval_seconds = SECURE_NETWORK_KEEPALIVE_INTERVAL_SECONDS;
    int probes = SECURE_NETWORK_KEEPALIVE_PROBES;

    if (fd < 0) {

        return;

    }
    setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &enabled, sizeof(enabled));

#ifdef TCP_KEEPIDLE
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &idle_seconds, sizeof(idle_seconds));
#endif

#ifdef TCP_KEEPINTVL
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &interval_seconds, sizeof(interval_seconds));
#endif

#ifdef TCP_KEEPCNT
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &probes, sizeof(probes));
#endif

#ifdef TCP_USER_TIMEOUT
    {
        int timeout_ms = SECURE_NETWORK_TCP_USER_TIMEOUT_MS;
        setsockopt(fd, IPPROTO_TCP, TCP_USER_TIMEOUT, &timeout_ms, sizeof(timeout_ms));
    }
#endif
}

void SECURE_NETWORK_disconnect(void) {
    /*
        Purpose: Disconnects the requested operation
        Returns: No value
    */

    pthread_mutex_lock(&Global_Secure_Client_Lock);
    Global_Secure_Client_Connection_Lost = 0;

    if (Global_Secure_Client_SSL && Global_Secure_Client_Authenticated) {

        secure_network_send_frame(Global_Secure_Client_SSL, SECURE_NETWORK_TYPE_LOGOUT, Global_Secure_Request_Id++,
                                  NULL, 0);

    }
    secure_network_close_client_locked();
    pthread_mutex_unlock(&Global_Secure_Client_Lock);
}

static int secure_network_connect_locked(char *error, size_t error_size) {
    /*
        Purpose: Connects the locked
        Returns: Success status
    */

    char host[INET_ADDRSTRLEN];
    struct sockaddr_in address;

    if (Global_Secure_Client_SSL) {

        return 1;

    }

    if (!SERVER_IDENTITY_get_trusted_host(host, sizeof(host))) {

        secure_network_set_error(error, error_size, "The trusted server has not been discovered on the LAN yet.");
        return 0;

    }
    Global_Secure_Client_Context = secure_network_create_client_context();

    if (!Global_Secure_Client_Context) {

        secure_network_set_error(error, error_size, "Unable to configure the post-quantum TLS client.");
        return 0;

    }
    Global_Secure_Client_Fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);

    if (Global_Secure_Client_Fd < 0) {

        goto failure;

    }
    secure_network_configure_client_keepalive(Global_Secure_Client_Fd);
    {
        struct timeval timeout = {SECURE_NETWORK_TIMEOUT_SECONDS, 0};
        setsockopt(Global_Secure_Client_Fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        setsockopt(Global_Secure_Client_Fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    }
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(SECURE_NETWORK_PORT);

    if (inet_pton(AF_INET, host, &address.sin_addr) != 1 ||
        connect(Global_Secure_Client_Fd, (struct sockaddr *)&address, sizeof(address)) != 0) {

        secure_network_set_error(error, error_size, "Unable to connect to the trusted server on port 47742.");
        goto failure;

    }
    Global_Secure_Client_SSL = SSL_new(Global_Secure_Client_Context);

    if (!Global_Secure_Client_SSL) {

        goto failure;

    }
    SSL_set_fd(Global_Secure_Client_SSL, Global_Secure_Client_Fd);

    if (SSL_connect(Global_Secure_Client_SSL) != 1 ||
        !secure_network_validate_negotiated_tls(Global_Secure_Client_SSL) ||
        !secure_network_client_identity_proof(Global_Secure_Client_SSL, error, error_size)) {

        goto failure;

    }
    return 1;

failure:
    secure_network_close_client_locked();

    if (error && error_size > 0 && error[0] == '\0') {

        secure_network_set_error(error, error_size, "The secure server connection failed.");

    }
    return 0;
}

static int secure_network_receive_status_locked(uint16_t request_type, uint32_t request_id, uint32_t *status,
                                                char *message, size_t message_size, unsigned char **extra,
                                                size_t *extra_size) {
    /*
        Purpose: Receives the status locked
        Returns: Success status
    */

    uint16_t type;
    uint32_t response_id;
    unsigned char *payload = NULL;
    size_t payload_size = 0;
    uint32_t message_length;

    if (!secure_network_receive_frame(Global_Secure_Client_SSL, &type, &response_id, &payload, &payload_size) ||
        type != (request_type | SECURE_NETWORK_RESPONSE_FLAG) || response_id != request_id || payload_size < 8) {

        OPENSSL_clear_free(payload, payload_size);
        return 0;

    }
    *status = secure_network_load_u32(payload);
    message_length = secure_network_load_u32(payload + 4);

    if ((size_t)message_length + 8 > payload_size) {

        OPENSSL_clear_free(payload, payload_size);
        return 0;

    }

    if (message && message_size > 0) {

        size_t copy = message_length < message_size - 1 ? message_length : message_size - 1;

        if (!sec_str_memcpy(message, message_size, (const char *)payload + 8, copy)) {

            OPENSSL_clear_free(payload, payload_size);
            return 0;

        }

    }

    if (extra && extra_size) {

        *extra_size = payload_size - 8 - message_length;
        *extra = NULL;

        if (*extra_size > 0) {

            *extra = OPENSSL_malloc(*extra_size);

            if (!*extra) {

                OPENSSL_clear_free(payload, payload_size);
                return 0;

            }

            if (!sec_memcpy(*extra, *extra_size, payload + 8 + message_length, *extra_size)) {

                OPENSSL_clear_free(*extra, *extra_size);
                *extra = NULL;
                *extra_size = 0;

                OPENSSL_clear_free(payload, payload_size);
                return 0;

            }

        }

    }
    OPENSSL_clear_free(payload, payload_size);
    return 1;
}

int SECURE_NETWORK_authenticate(const char *username, const char *password, const char *totp, int *is_admin,
                                char *error, size_t error_size) {
    /*
        Purpose: Authenticates the requested operation
        Returns: Success status
    */

    size_t username_size = username ? strlen(username) : 0;
    size_t password_size = password ? strlen(password) : 0;
    size_t totp_size = totp ? strlen(totp) : 0;
    size_t payload_size = 8 + username_size + password_size + totp_size;
    unsigned char *payload = NULL;
    uint32_t request_id, status;
    unsigned char *extra = NULL;
    size_t extra_size = 0;
    int result = SECURE_NETWORK_AUTH_ERROR;

    if (error && error_size > 0) {

        error[0] = '\0';

    }

    if (!username || !password || username_size == 0 || username_size > 63 || password_size == 0 ||
        password_size > 127 || totp_size > 8) {

        secure_network_set_error(error, error_size, "Invalid remote authentication request.");
        return SECURE_NETWORK_AUTH_ERROR;

    }

    pthread_mutex_lock(&Global_Secure_Client_Lock);

    if (!secure_network_connect_locked(error, error_size)) {

        goto cleanup;

    }
    payload = OPENSSL_zalloc(payload_size);

    if (!payload) {

        goto cleanup;

    }
    secure_network_store_u16(payload, (uint16_t)username_size);
    secure_network_store_u16(payload + 2, (uint16_t)password_size);
    secure_network_store_u16(payload + 4, (uint16_t)totp_size);
    secure_network_store_u16(payload + 6, SECURE_NETWORK_AUTH_MODE_LOGIN);

    if (!sec_memcpy(payload + 8, payload_size - 8, username, username_size) ||
        !sec_memcpy(payload + 8 + username_size, payload_size - 8 - username_size, password, password_size) ||
        (totp_size > 0 && !sec_memcpy(payload + 8 + username_size + password_size,
                                      payload_size - 8 - username_size - password_size, totp, totp_size))) {

        secure_network_set_error(error, error_size, "Unable to safely construct authentication payload.");
        goto cleanup;

    }

    request_id = Global_Secure_Request_Id++;

    if (!secure_network_send_frame(Global_Secure_Client_SSL, SECURE_NETWORK_TYPE_AUTH, request_id, payload,
                                   payload_size) ||
        !secure_network_receive_status_locked(SECURE_NETWORK_TYPE_AUTH, request_id, &status, error, error_size, &extra,
                                              &extra_size)) {

        secure_network_set_error(error, error_size, "The encrypted authentication exchange failed.");
        secure_network_close_client_locked();
        goto cleanup;

    }

    if (status == SECURE_NETWORK_STATUS_OK) {

        /*
            The 10-second socket timeout is only for connection and login.
            Clear it after authentication so a temporarily slow request does not
            get mistaken for a lost server connection and force a random logout.
        */

        if (Global_Secure_Client_Fd >= 0) {

            struct timeval no_timeout = {0, 0};

            setsockopt(Global_Secure_Client_Fd, SOL_SOCKET, SO_RCVTIMEO, &no_timeout, sizeof(no_timeout));
            setsockopt(Global_Secure_Client_Fd, SOL_SOCKET, SO_SNDTIMEO, &no_timeout, sizeof(no_timeout));

        }

        Global_Secure_Client_Authenticated = 1;
        Global_Secure_Client_Connection_Lost = 0;

        if (is_admin) {

            *is_admin = extra_size >= 4 && secure_network_load_u32(extra) != 0;

        }
        result = SECURE_NETWORK_AUTH_SUCCESS;

    }

    else if (status == SECURE_NETWORK_STATUS_TOTP_REQUIRED) {

        result = SECURE_NETWORK_AUTH_TOTP_REQUIRED;

    }

cleanup:
    OPENSSL_clear_free(payload, payload_size);
    OPENSSL_clear_free(extra, extra_size);
    pthread_mutex_unlock(&Global_Secure_Client_Lock);
    return result;
}

int SECURE_NETWORK_verify_password(const char *username, const char *password, char *error, size_t error_size) {
    /*
        Purpose: Re-verifies the active remote account password
        Returns: Success status
    */

    size_t username_size = username ? strlen(username) : 0;
    size_t password_size = password ? strlen(password) : 0;
    size_t payload_size = 8U + username_size + password_size;
    unsigned char *payload = NULL;
    uint32_t request_id;
    uint32_t status = SECURE_NETWORK_STATUS_ERROR;
    int success = 0;

    if (error && error_size > 0) {

        error[0] = '\0';

    }

    if (!username || username_size == 0 || username_size > 63 || !password || password_size == 0 ||
        password_size > 127) {

        secure_network_set_error(error, error_size, "Enter a valid password.");
        return 0;

    }

    pthread_mutex_lock(&Global_Secure_Client_Lock);

    if (!Global_Secure_Client_Authenticated || !Global_Secure_Client_SSL) {

        secure_network_set_error(error, error_size, "The remote authenticated session is unavailable.");
        goto cleanup;

    }

    payload = OPENSSL_zalloc(payload_size);

    if (!payload) {

        secure_network_set_error(error, error_size, "Unable to prepare password verification.");
        goto cleanup;

    }

    secure_network_store_u16(payload, (uint16_t)username_size);
    secure_network_store_u16(payload + 2, (uint16_t)password_size);
    secure_network_store_u16(payload + 4, 0U);
    secure_network_store_u16(payload + 6, SECURE_NETWORK_AUTH_MODE_REAUTH);

    if (!sec_memcpy(payload + 8, payload_size - 8, username, username_size) ||
        !sec_memcpy(payload + 8 + username_size, payload_size - 8 - username_size, password, password_size)) {

        secure_network_set_error(error, error_size, "Unable to safely construct password-verification payload.");
        goto cleanup;

    }

    request_id = Global_Secure_Request_Id++;

    if (!secure_network_send_frame(Global_Secure_Client_SSL, SECURE_NETWORK_TYPE_AUTH, request_id, payload,
                                   payload_size) ||
        !secure_network_receive_status_locked(SECURE_NETWORK_TYPE_AUTH, request_id, &status, error, error_size, NULL,
                                              NULL)) {

        secure_network_set_error(error, error_size, "The encrypted password verification exchange failed.");
        secure_network_close_client_locked();
        goto cleanup;

    }

    /*
        Older servers ignore the authentication-mode field and process this as a
        normal login. A TOTP-required response still proves that the supplied
        password was correct, while newer servers return OK for re-verification.
    */
    success = status == SECURE_NETWORK_STATUS_OK || status == SECURE_NETWORK_STATUS_TOTP_REQUIRED;

cleanup:
    OPENSSL_clear_free(payload, payload_size);
    pthread_mutex_unlock(&Global_Secure_Client_Lock);
    return success;
}

int SECURE_NETWORK_is_authenticated_remote(void) {
    /*
        Purpose: Checks whether the authenticated is remote
        Returns: Boolean status
    */

    int result;
    pthread_mutex_lock(&Global_Secure_Client_Lock);
    result = Global_Secure_Client_Authenticated || Global_Secure_Client_Connection_Lost;
    pthread_mutex_unlock(&Global_Secure_Client_Lock);
    return result;
}

int SECURE_NETWORK_remote_connection_lost(void) {
    /*
        Purpose: Detects and consumes a failed authenticated remote-server session
        Returns: Boolean connection-loss status
    */

    int connection_lost = 0;

    pthread_mutex_lock(&Global_Secure_Client_Lock);

    if (Global_Secure_Client_Authenticated && Global_Secure_Client_SSL && Global_Secure_Client_Fd >= 0) {

        struct pollfd descriptor;
        int poll_result;

        memset(&descriptor, 0, sizeof(descriptor));
        descriptor.fd = Global_Secure_Client_Fd;
        descriptor.events = POLLIN | POLLERR | POLLHUP;

#ifdef POLLRDHUP
        descriptor.events |= POLLRDHUP;
#endif

        poll_result = poll(&descriptor, 1, 0);

        if (poll_result < 0 && errno != EINTR) {

            secure_network_abort_client_locked();

        }

        else if (poll_result > 0) {

            short failure_events = POLLERR | POLLHUP | POLLNVAL;

#ifdef POLLRDHUP
            failure_events |= POLLRDHUP;
#endif

            if ((descriptor.revents & failure_events) != 0) {

                secure_network_abort_client_locked();

            }

            else if ((descriptor.revents & POLLIN) != 0) {

                unsigned char byte;
                ssize_t received = recv(Global_Secure_Client_Fd, &byte, sizeof(byte), MSG_PEEK | MSG_DONTWAIT);

                if (received == 0 || (received < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {

                    secure_network_abort_client_locked();

                }

            }

        }

    }

    connection_lost = Global_Secure_Client_Connection_Lost;
    Global_Secure_Client_Connection_Lost = 0;
    pthread_mutex_unlock(&Global_Secure_Client_Lock);
    return connection_lost;
}

const char *SECURE_NETWORK_status(void) {
    /*
        Purpose: Gets the requested item status
        Returns: Text pointer
    */

    return Global_Secure_Status;
}

static int secure_network_request_locked(uint16_t type, const void *payload, size_t payload_size, uint32_t *status,
                                         char *error, size_t error_size, unsigned char **extra, size_t *extra_size) {
    /*
        Purpose: Sends a secure network request while holding the connection lock
        Returns: Success status
    */

    uint32_t request_id;

    if (!Global_Secure_Client_Authenticated || !Global_Secure_Client_SSL) {

        secure_network_set_error(error, error_size, "No authenticated remote-server session exists.");
        return 0;

    }
    request_id = Global_Secure_Request_Id++;

    if (!secure_network_send_frame(Global_Secure_Client_SSL, type, request_id, payload, payload_size) ||
        !secure_network_receive_status_locked(type, request_id, status, error, error_size, extra, extra_size)) {

        secure_network_abort_client_locked();
        secure_network_set_error(error, error_size, "The encrypted server request failed.");
        return 0;

    }
    return 1;
}

int SECURE_NETWORK_save_document(const char *document_kind, const char *document_name, const char *case_number,
                                 const void *content, size_t content_size, char *error, size_t error_size) {
    /*
        Purpose: Saves the document
        Returns: Success status
    */

    size_t kind_size = document_kind ? strlen(document_kind) : 0;
    size_t name_size = document_name ? strlen(document_name) : 0;
    size_t case_size = case_number ? strlen(case_number) : 0;
    size_t payload_size = 12 + kind_size + name_size + case_size + content_size;
    unsigned char *payload;
    size_t offset = 12;
    uint32_t status;
    int success = 0;

    if (kind_size == 0 || kind_size > 63 || name_size == 0 || name_size >= DATASTORE_DOCUMENT_NAME_MAX ||
        case_size >= DATASTORE_CASE_NUMBER_MAX || content_size > 64U * 1024U * 1024U ||
        (content_size > 0 && !content)) {

        secure_network_set_error(error, error_size, "Invalid remote document save request.");
        return 0;

    }
    payload = OPENSSL_malloc(payload_size);

    if (!payload) {

        return 0;

    }
    secure_network_store_u16(payload, (uint16_t)kind_size);
    secure_network_store_u16(payload + 2, (uint16_t)name_size);
    secure_network_store_u16(payload + 4, (uint16_t)case_size);
    secure_network_store_u16(payload + 6, 0);
    secure_network_store_u32(payload + 8, (uint32_t)content_size);

    if (!sec_memcpy(payload + offset, payload_size - offset, document_kind, kind_size)) {

        OPENSSL_clear_free(payload, payload_size);
        return 0;

    }

    offset += kind_size;

    if (!sec_memcpy(payload + offset, payload_size - offset, document_name, name_size)) {

        OPENSSL_clear_free(payload, payload_size);
        return 0;

    }

    offset += name_size;

    if (case_size > 0) {

        if (!sec_memcpy(payload + offset, payload_size - offset, case_number, case_size)) {

            OPENSSL_clear_free(payload, payload_size);
            return 0;

        }

        offset += case_size;

    }

    if (content_size > 0) {

        if (!sec_memcpy(payload + offset, payload_size - offset, content, content_size)) {

            OPENSSL_clear_free(payload, payload_size);
            return 0;

        }

    }

    pthread_mutex_lock(&Global_Secure_Client_Lock);
    success = secure_network_request_locked(SECURE_NETWORK_TYPE_SAVE, payload, payload_size, &status, error, error_size,
                                            NULL, NULL) &&
              status == SECURE_NETWORK_STATUS_OK;
    pthread_mutex_unlock(&Global_Secure_Client_Lock);
    OPENSSL_clear_free(payload, payload_size);
    return success;
}

int SECURE_NETWORK_load_document(const char *document_kind, const char *document_name, unsigned char **content,
                                 size_t *content_size, int *found, char *error, size_t error_size) {
    /*
        Purpose: Loads the document
        Returns: Success status
    */

    size_t kind_size = document_kind ? strlen(document_kind) : 0;
    size_t name_size = document_name ? strlen(document_name) : 0;
    size_t payload_size = 4 + kind_size + name_size;
    unsigned char *payload, *extra = NULL;
    size_t extra_size = 0;
    uint32_t status, found_value, length;
    int success = 0;

    if (!content || !content_size || !found || kind_size == 0 || kind_size > 63 || name_size == 0 ||
        name_size >= DATASTORE_DOCUMENT_NAME_MAX) {

        return 0;

    }
    *content = NULL;
    *content_size = 0;
    *found = 0;
    payload = OPENSSL_malloc(payload_size);

    if (!payload) {

        return 0;

    }
    secure_network_store_u16(payload, (uint16_t)kind_size);
    secure_network_store_u16(payload + 2, (uint16_t)name_size);

    if (!sec_memcpy(payload + 4, payload_size - 4, document_kind, kind_size) ||
        !sec_memcpy(payload + 4 + kind_size, payload_size - 4 - kind_size, document_name, name_size)) {

        OPENSSL_clear_free(payload, payload_size);
        return 0;

    }

    pthread_mutex_lock(&Global_Secure_Client_Lock);

    if (!secure_network_request_locked(SECURE_NETWORK_TYPE_LOAD, payload, payload_size, &status, error, error_size,
                                       &extra, &extra_size) ||
        status != SECURE_NETWORK_STATUS_OK || extra_size < 8) {

        goto cleanup;

    }
    found_value = secure_network_load_u32(extra);
    length = secure_network_load_u32(extra + 4);

    if ((size_t)length + 8 != extra_size) {

        goto cleanup;

    }
    *found = found_value != 0;

    if (*found && length > 0) {

        *content = sec_calloc_array((size_t)length + 1U, 1U, SECURE_NETWORK_MAX_PAYLOAD + 1U);

        if (!*content) {

            goto cleanup;

        }

        if (!sec_memcpy(*content, (size_t)length + 1U, extra + 8, length)) {

            goto cleanup;

        }

        *content_size = (size_t)length;

    }
    success = 1;

cleanup:
    pthread_mutex_unlock(&Global_Secure_Client_Lock);
    OPENSSL_clear_free(payload, payload_size);
    OPENSSL_clear_free(extra, extra_size);

    if (!success) {

        free(*content);
        *content = NULL;
        *content_size = 0;
        *found = 0;

    }
    return success;
}

int SECURE_NETWORK_delete_document(const char *document_kind, const char *document_name, int *deleted, char *error,
                                   size_t error_size) {
    /*
        Purpose: Deletes the document
        Returns: Success status
    */

    size_t kind_size = document_kind ? strlen(document_kind) : 0;
    size_t name_size = document_name ? strlen(document_name) : 0;
    size_t payload_size = 4 + kind_size + name_size;
    unsigned char *payload = NULL;
    unsigned char *extra = NULL;
    size_t extra_size = 0;
    uint32_t status = SECURE_NETWORK_STATUS_ERROR;
    int success = 0;

    if (deleted) {

        *deleted = 0;

    }

    if (!deleted || kind_size == 0 || kind_size > 63 || name_size == 0 || name_size >= DATASTORE_DOCUMENT_NAME_MAX) {

        secure_network_set_error(error, error_size, "Invalid remote document delete request.");
        return 0;

    }

    payload = OPENSSL_malloc(payload_size);

    if (!payload) {

        secure_network_set_error(error, error_size, "Out of memory while creating delete request.");
        return 0;

    }

    secure_network_store_u16(payload, (uint16_t)kind_size);
    secure_network_store_u16(payload + 2, (uint16_t)name_size);

    if (!sec_memcpy(payload + 4, payload_size - 4, document_kind, kind_size) ||
        !sec_memcpy(payload + 4 + kind_size, payload_size - 4 - kind_size, document_name, name_size)) {

        OPENSSL_clear_free(payload, payload_size);
        return 0;

    }

    pthread_mutex_lock(&Global_Secure_Client_Lock);

    if (!secure_network_request_locked(SECURE_NETWORK_TYPE_DELETE, payload, payload_size, &status, error, error_size,
                                       &extra, &extra_size) ||
        status != SECURE_NETWORK_STATUS_OK || extra_size != 4) {

        goto cleanup;

    }

    *deleted = secure_network_load_u32(extra) != 0;
    success = 1;

cleanup:
    pthread_mutex_unlock(&Global_Secure_Client_Lock);
    OPENSSL_clear_free(payload, payload_size);
    OPENSSL_clear_free(extra, extra_size);
    return success;
}

int SECURE_NETWORK_list_documents(const char *document_kind, Type_DataStore_Document_Summary *documents,
                                  size_t capacity, size_t *count, char *error, size_t error_size) {
    /*
        Purpose: Lists the documents
        Returns: Success status
    */

    size_t kind_size = document_kind ? strlen(document_kind) : 0;
    unsigned char payload[66], *extra = NULL;
    size_t extra_size = 0, offset = 4, available;
    uint32_t status, total;
    int success = 0;

    if (!documents || !count || kind_size == 0 || kind_size > 63) {

        return 0;

    }
    *count = 0;
    secure_network_store_u16(payload, (uint16_t)kind_size);

    if (!sec_memcpy(payload + 2, sizeof(payload) - 2, document_kind, kind_size)) {

        return 0;

    }

    pthread_mutex_lock(&Global_Secure_Client_Lock);

    if (!secure_network_request_locked(SECURE_NETWORK_TYPE_LIST, payload, 2 + kind_size, &status, error, error_size,
                                       &extra, &extra_size) ||
        status != SECURE_NETWORK_STATUS_OK || extra_size < 4) {

        goto cleanup;

    }
    total = secure_network_load_u32(extra);
    available = total < capacity ? total : capacity;
    for (size_t i = 0; i < total; i++) {
        uint16_t name_size, case_size;
        uint64_t updated;

        if (offset + 12 > extra_size) {

            goto cleanup;

        }
        name_size = secure_network_load_u16(extra + offset);
        case_size = secure_network_load_u16(extra + offset + 2);
        updated = secure_network_load_u64(extra + offset + 4);
        offset += 12;

        if (offset + name_size + case_size > extra_size || name_size >= DATASTORE_DOCUMENT_NAME_MAX ||
            case_size >= DATASTORE_CASE_NUMBER_MAX) {

            goto cleanup;

        }

        if (i < available) {

            if (!sec_str_memcpy(documents[i].document_name, sizeof(documents[i].document_name),
                                (const char *)extra + offset, name_size) ||
                !sec_str_memcpy(documents[i].case_number, sizeof(documents[i].case_number),
                                (const char *)extra + offset + name_size, case_size)) {

                goto cleanup;

            }

            documents[i].updated_at = (long long)updated;

        }
        offset += name_size + case_size;
    }

    if (offset != extra_size) {

        goto cleanup;

    }
    *count = available;
    success = 1;

cleanup:
    pthread_mutex_unlock(&Global_Secure_Client_Lock);
    OPENSSL_clear_free(extra, extra_size);
    return success;
}

int SECURE_NETWORK_list_users(Type_Auth_User_Summary *users, size_t capacity, size_t *count, char *error,
                              size_t error_size) {
    /*
        Purpose: Lists the users
        Returns: Success status
    */

    uint32_t status, total;
    unsigned char *extra = NULL;
    size_t extra_size = 0;
    size_t offset = 4;
    size_t available;
    int success = 0;

    if (!users || !count || capacity == 0) {

        secure_network_set_error(error, error_size, "Invalid user-list request.");
        return 0;

    }
    *count = 0;
    pthread_mutex_lock(&Global_Secure_Client_Lock);

    if (!secure_network_request_locked(SECURE_NETWORK_TYPE_USER_LIST, NULL, 0, &status, error, error_size, &extra,
                                       &extra_size) ||
        status != SECURE_NETWORK_STATUS_OK || extra_size < 4) {

        goto cleanup;

    }
    total = secure_network_load_u32(extra);
    available = total < capacity ? total : capacity;
    for (size_t i = 0; i < total; i++) {
        uint16_t username_size;
        uint16_t role;
        uint32_t is_admin;
        uint32_t totp_enabled;
        uint64_t created_at;

        if (offset + 20 > extra_size) {

            goto cleanup;

        }
        username_size = secure_network_load_u16(extra + offset);
        role = secure_network_load_u16(extra + offset + 2);
        is_admin = secure_network_load_u32(extra + offset + 4);
        totp_enabled = secure_network_load_u32(extra + offset + 8);
        created_at = secure_network_load_u64(extra + offset + 12);
        offset += 20;

        if (offset + username_size > extra_size || username_size > AUTH_PUBLIC_USERNAME_MAX) {

            goto cleanup;

        }

        if (i < available) {

            memset(&users[i], 0, sizeof(users[i]));

            if (!sec_str_memcpy(users[i].username, sizeof(users[i].username), (const char *)extra + offset,
                                username_size)) {

                goto cleanup;

            }

            users[i].role = role <= AUTH_ROLE_ADMIN ? (int)role : (is_admin ? AUTH_ROLE_CO_ADMIN : AUTH_ROLE_USER);
            users[i].is_admin = users[i].role >= AUTH_ROLE_CO_ADMIN;
            users[i].totp_enabled = totp_enabled != 0;
            users[i].created_at = (int64_t)created_at;

        }
        offset += username_size;
    }

    if (offset != extra_size) {

        goto cleanup;

    }
    *count = available;
    success = 1;

cleanup:
    pthread_mutex_unlock(&Global_Secure_Client_Lock);
    OPENSSL_clear_free(extra, extra_size);
    return success;
}

static int secure_network_admin_request(uint16_t action, const char *username, const char *password,
                                        const unsigned char *secret, size_t secret_size, int role, unsigned int flags,
                                        char *error, size_t error_size) {
    /*
        Purpose: Builds and sends an authenticated account-management request to the secure server
        Returns: Success status
    */

    size_t username_size = username ? strlen(username) : 0;
    size_t password_size = password ? strlen(password) : 0;
    size_t payload_size;
    size_t offset = SECURE_NETWORK_USER_ADMIN_HEADER_BYTES;
    unsigned char *payload = NULL;
    unsigned char *extra = NULL;
    size_t extra_size = 0;
    uint32_t status = SECURE_NETWORK_STATUS_ERROR;
    int success = 0;

    if (username_size == 0 || username_size > AUTH_PUBLIC_USERNAME_MAX || password_size > 127 ||
        secret_size > AUTH_PUBLIC_TOTP_SECRET_BYTES || (secret_size > 0 && !secret)) {

        secure_network_set_error(error, error_size, "Invalid account-management request.");
        return 0;

    }

    payload_size = SECURE_NETWORK_USER_ADMIN_HEADER_BYTES + username_size + password_size + secret_size;
    payload = OPENSSL_zalloc(payload_size);

    if (!payload) {

        secure_network_set_error(error, error_size, "Unable to allocate the account-management request.");
        return 0;

    }

    secure_network_store_u16(payload, action);
    secure_network_store_u16(payload + 2, (uint16_t)username_size);
    secure_network_store_u16(payload + 4, (uint16_t)password_size);
    secure_network_store_u16(payload + 6, (uint16_t)secret_size);
    secure_network_store_u16(payload + 8, (uint16_t)role);
    secure_network_store_u16(payload + 10, (uint16_t)flags);

    if (!sec_memcpy(payload + offset, payload_size - offset, username, username_size)) {

        secure_network_set_error(error, error_size, "Unable to safely construct account-management request.");
        OPENSSL_clear_free(payload, payload_size);
        return 0;

    }

    offset += username_size;

    if (password_size > 0) {

        if (!sec_memcpy(payload + offset, payload_size - offset, password, password_size)) {

            secure_network_set_error(error, error_size, "Unable to safely construct account-management request.");
            OPENSSL_clear_free(payload, payload_size);
            return 0;

        }

        offset += password_size;

    }

    if (secret_size > 0) {

        if (!sec_memcpy(payload + offset, payload_size - offset, secret, secret_size)) {

            secure_network_set_error(error, error_size, "Unable to safely construct account-management request.");
            OPENSSL_clear_free(payload, payload_size);
            return 0;

        }

    }

    pthread_mutex_lock(&Global_Secure_Client_Lock);

    if (secure_network_request_locked(SECURE_NETWORK_TYPE_USER_ADMIN, payload, payload_size, &status, error, error_size,
                                      &extra, &extra_size) &&
        status == SECURE_NETWORK_STATUS_OK) {

        success = 1;

    }
    pthread_mutex_unlock(&Global_Secure_Client_Lock);
    OPENSSL_clear_free(payload, payload_size);
    OPENSSL_clear_free(extra, extra_size);
    return success;
}

int SECURE_NETWORK_admin_create_user(const char *username, const char *password, int enable_totp,
                                     const unsigned char *totp_secret, char *error, size_t error_size) {
    /*
        Purpose: Requests creation of a user account through the secure network connection
        Returns: Success status
    */

    return secure_network_admin_request(SECURE_NETWORK_USER_ADMIN_CREATE, username, password,
                                        enable_totp ? totp_secret : NULL,
                                        enable_totp ? AUTH_PUBLIC_TOTP_SECRET_BYTES : 0, AUTH_ROLE_USER,
                                        enable_totp ? SECURE_NETWORK_USER_ADMIN_ENABLE_TOTP : 0, error, error_size);
}

int SECURE_NETWORK_admin_reset_password(const char *username, const char *new_password, char *error,
                                        size_t error_size) {
    /*
        Purpose: Requests a password reset for a user account through the secure network connection
        Returns: Success status
    */

    return secure_network_admin_request(SECURE_NETWORK_USER_ADMIN_RESET_PASSWORD, username, new_password, NULL, 0,
                                        AUTH_ROLE_USER, 0, error, error_size);
}

int SECURE_NETWORK_admin_set_totp(const char *username, const unsigned char secret[AUTH_PUBLIC_TOTP_SECRET_BYTES],
                                  char *error, size_t error_size) {
    /*
        Purpose: Requests enabling or replacing TOTP authentication for a user account
        Returns: Success status
    */

    return secure_network_admin_request(SECURE_NETWORK_USER_ADMIN_SET_TOTP, username, NULL, secret,
                                        AUTH_PUBLIC_TOTP_SECRET_BYTES, AUTH_ROLE_USER, 0, error, error_size);
}

int SECURE_NETWORK_admin_remove_totp(const char *username, char *error, size_t error_size) {
    /*
        Purpose: Requests removal of TOTP authentication from a user account
        Returns: Success status
    */

    return secure_network_admin_request(SECURE_NETWORK_USER_ADMIN_REMOVE_TOTP, username, NULL, NULL, 0, AUTH_ROLE_USER,
                                        0, error, error_size);
}

int SECURE_NETWORK_admin_set_role(const char *username, int role, char *error, size_t error_size) {
    /*
        Purpose: Requests a role change for a user account through the secure network connection
        Returns: Success status
    */

    return secure_network_admin_request(SECURE_NETWORK_USER_ADMIN_SET_ROLE, username, NULL, NULL, 0, role, 0, error,
                                        error_size);
}

int SECURE_NETWORK_admin_delete_user(const char *username, char *error, size_t error_size) {
    /*
        Purpose: Requests deletion of a user account through the secure network connection
        Returns: Success status
    */

    return secure_network_admin_request(SECURE_NETWORK_USER_ADMIN_DELETE, username, NULL, NULL, 0, AUTH_ROLE_USER, 0,
                                        error, error_size);
}
