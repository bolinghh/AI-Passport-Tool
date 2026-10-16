#ifndef PASSPORT_CORE_H
#define PASSPORT_CORE_H
/* Strings are UTF-8. Every returned string is owned and must be freed once.
 * Handles may not be destroyed while a call is outstanding. Commands are async;
 * an empty return acknowledges queue admission, not completion on the device. */
void *passport_core_create(void);
char *passport_core_send(void *core, const char *json);
char *passport_core_snapshot(void *core);
char *passport_audio_outputs(void);
void passport_string_free(char *value);
void passport_core_destroy(void *core);
#endif
