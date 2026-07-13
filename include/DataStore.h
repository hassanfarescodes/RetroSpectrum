#ifndef RETROSPECTRUM_DATA_STORE_H
#define RETROSPECTRUM_DATA_STORE_H

#include <stddef.h>

#define DATASTORE_DOCUMENT_NAME_MAX 512
#define DATASTORE_CASE_NUMBER_MAX 128

#define DATASTORE_KIND_CASE_MANAGEMENT "case_management"
#define DATASTORE_KIND_CLASSIFICATION "classification"

typedef struct Type_DataStore_Document_Summary {
    char document_name[DATASTORE_DOCUMENT_NAME_MAX];
    char case_number[DATASTORE_CASE_NUMBER_MAX];
    long long updated_at;
} Type_DataStore_Document_Summary;

int DATASTORE_get_path(char *path, size_t path_size);

int DATASTORE_save_content(const char *document_kind,
                           const char *document_name,
                           const char *case_number,
                           const void *content,
                           size_t content_size,
                           char *error,
                           size_t error_size);

int DATASTORE_load_content(const char *document_kind,
                           const char *document_name,
                           unsigned char **content,
                           size_t *content_size,
                           int *found,
                           char *error,
                           size_t error_size);

void DATASTORE_free_content(unsigned char *content, size_t content_size);

int DATASTORE_list_documents(const char *document_kind,
                             Type_DataStore_Document_Summary *documents,
                             size_t capacity,
                             size_t *count,
                             char *error,
                             size_t error_size);

/* Server-side entry points bypass the remote-client router. */
int DATASTORE_server_save_content(const char *document_kind,
                                  const char *document_name,
                                  const char *case_number,
                                  const void *content,
                                  size_t content_size,
                                  char *error,
                                  size_t error_size);
int DATASTORE_server_load_content(const char *document_kind,
                                  const char *document_name,
                                  unsigned char **content,
                                  size_t *content_size,
                                  int *found,
                                  char *error,
                                  size_t error_size);
int DATASTORE_server_list_documents(const char *document_kind,
                                    Type_DataStore_Document_Summary *documents,
                                    size_t capacity,
                                    size_t *count,
                                    char *error,
                                    size_t error_size);

#endif
