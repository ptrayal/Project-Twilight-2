/*
 * Written by Solar Designer <solar at openwall.com> in 2000-2011.
 * No copyright is claimed, and the software is hereby placed in the public
 * domain.
 *
 * Internal header for crypt_blowfish.c — declares the three functions it
 * exports so that crypt_gensalt.c and ow-crypt.c can include it to ensure
 * their call sites match the actual definitions.
 */

#ifndef CRYPT_BLOWFISH_H
#define CRYPT_BLOWFISH_H

extern int   _crypt_output_magic(const char *setting, char *output, int size);

extern char *_crypt_blowfish_rn(const char *key, const char *setting,
                                char *output, int size);

extern char *_crypt_gensalt_blowfish_rn(const char *prefix,
                                        unsigned long count,
                                        const char *input, int size,
                                        char *output, int output_size);

#endif /* CRYPT_BLOWFISH_H */
