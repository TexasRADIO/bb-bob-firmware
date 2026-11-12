/* SPDX-License-Identifier: GPL-3.0-only; generated from identity.json. */
#ifndef SW_BOB_IDENTITY_H
#define SW_BOB_IDENTITY_H
#define SW_BOB_DISPLAY_NAME "BB-BOB"
#define SW_BOB_VERSION "R34"
#define SW_BOB_DISPLAY_VERSION "BB-BOB R34"
#define SW_BOB_PRODUCT_ID "FM-1_92201029"
extern char sw_bob_app_sha_prefix[9];
void sw_bob_hash_application(const unsigned char *, unsigned int, void (*)(void));
#endif
