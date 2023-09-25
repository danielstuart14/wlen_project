#include <tinycrypt/ecc_dsa.h>
#include <tinycrypt/sha256.h>
#include <tinycrypt/constants.h>
#include <tinycrypt/ctr_prng.h>
#include <zephyr/random/rand32.h>
#include <string.h>

#include "digital_sign.h"

static uint8_t key_bytes[NUM_ECC_BYTES];
static uint8_t digest_bytes[TC_SHA256_DIGEST_SIZE];

static struct tc_sha256_state_struct ctx;

static TCCtrPrng_t prng_state;
static bool prng_init = false;
static int setup_prng(void)
{
	if (prng_init)
		return 0;

	uint8_t entropy[TC_AES_KEY_SIZE + TC_AES_BLOCK_SIZE];

	for (int i = 0; i < sizeof(entropy); i += sizeof(uint32_t)) {
		uint32_t rv = sys_rand32_get();

		memcpy(entropy + i, &rv, sizeof(uint32_t));
	}

	int res = tc_ctr_prng_init(&prng_state,
				   (const uint8_t *) &entropy, sizeof(entropy),
				   NULL,
				   0);

    if (res != TC_CRYPTO_SUCCESS)
        return 1;
    
    prng_init = true;
    return 0;
}

int default_CSPRNG(uint8_t *dest, unsigned int size)
{
	int res = tc_ctr_prng_generate(&prng_state, NULL, 0, dest, size);
	return res;
}

void set_key_ecdsa(const uint8_t *key) {
	memcpy(key_bytes, key, NUM_ECC_BYTES);
}

int init_ecdsa() {
	int res = setup_prng();
	if (res)
		return 1;
	
	uECC_set_rng(&default_CSPRNG);
	return 0;
}

int sign_ecdsa(struct EcdsaData_t *data) {
	int res = tc_sha256_init(&ctx);
	if (res != TC_CRYPTO_SUCCESS)
		return 1;

	tc_sha256_update(&ctx, data->message, data->length);
	tc_sha256_final(digest_bytes, &ctx);

	res = uECC_sign(key_bytes, digest_bytes, sizeof(digest_bytes), data->signature, uECC_secp256r1());
	if (res != TC_CRYPTO_SUCCESS)
		return 2;

	return 0;
}
