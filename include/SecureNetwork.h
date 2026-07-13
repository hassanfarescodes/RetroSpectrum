#ifndef RETROSPECTRUM_SECURE_NETWORK_H
#define RETROSPECTRUM_SECURE_NETWORK_H

#include <stddef.h>
#include "DataStore.h"
#include "AuthTypes.h"

#define SECURE_NETWORK_PORT 47742

#define SECURE_NETWORK_AUTH_ERROR 0
#define SECURE_NETWORK_AUTH_SUCCESS 1
#define SECURE_NETWORK_AUTH_TOTP_REQUIRED 2

int SECURE_NETWORK_start_server(char *error, size_t error_size);
void SECURE_NETWORK_stop_server(void);

int SECURE_NETWORK_authenticate(const char *username, const char *password, const char *totp,
                                int *is_admin, char *error, size_t error_size);
void SECURE_NETWORK_disconnect(void);
int SECURE_NETWORK_is_authenticated_remote(void);
const char *SECURE_NETWORK_status(void);

int SECURE_NETWORK_save_document(const char *document_kind, const char *document_name,
                                 const char *case_number, const void *content, size_t content_size,
                                 char *error, size_t error_size);
int SECURE_NETWORK_load_document(const char *document_kind, const char *document_name,
                                 unsigned char **content, size_t *content_size, int *found,
                                 char *error, size_t error_size);
int SECURE_NETWORK_list_documents(const char *document_kind,
                                  Type_DataStore_Document_Summary *documents,
                                  size_t capacity, size_t *count,
                                  char *error, size_t error_size);
int SECURE_NETWORK_list_users(Type_Auth_User_Summary *users, size_t capacity, size_t *count,
                              char *error, size_t error_size);

#endif
