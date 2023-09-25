#ifndef _DIGITAL_SIGN_H_
#define _DIGITAL_SIGN_H_

#include <zephyr/types.h>
#include <tinycrypt/ecc_dsa.h>

#define KEY_SIZE NUM_ECC_BYTES
#define SIGNATURE_SIZE (2*NUM_ECC_BYTES)

struct EcdsaData_t {
    uint8_t *signature;
    const uint8_t *message;
    size_t length;
};

void set_key_ecdsa(const uint8_t *);
int init_ecdsa();
int sign_ecdsa(struct EcdsaData_t *);

#endif