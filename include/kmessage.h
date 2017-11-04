#pragma once

#include <linux/uidgid.h>

#include "kmessaged.h"

struct kmessage_t {
    char *msg;
    unsigned long len;
    uid_t uid;
};

/**
 * kmessaged_msg_init
 *
 * Initializes a new `kmessage_t` object on given struct pointer in kernel address space.
 *
 * @param msgstruct Pointer to the object being initialized.
 * @param len Length of the message.
 * @param uid The `uid` value of the user.
 *
 * @return 0 if successful, EINVAL if invalid parameter provided, ENOMEM if no memory has left on device.
 */
KMESSAGED_EXPORT int kmessaged_msg_init(const kmessage_t *msgstruct, const unsigned long len, const uid_t uid);

/**
 * kmessaged_msg_create
 *
 * Creates a new message with given C string.
 *
 * @param msgstruct Pointer to the object being initialized.
 * @param msgdata The *null-terminated* C string to be used in message.
 * @param uid The `uid` value of the user.
 *
 * @return 0 if successful, EINVAL if invalid parameter provided, ENOMEM if no memory has left on device.
 */
KMESSAGED_EXPORT int kmessaged_msg_create(const kmessage_t *msgstruct, const char *msgdata, const uid_t uid);

/**
 * kmessaged_msg_release
 *
 * Releases the given message struct.
 *
 * @param msgstruct Pointer to the object going to be released.
 * 
 * @return This function always returns 0.
 */
KMESSAGED_EXPORT int kmessaged_msg_release(const kmessage_t *msgstruct);
