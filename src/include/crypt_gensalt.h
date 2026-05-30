/*
 * Written by Solar Designer <solar at openwall.com> in 2000-2011.
 * No copyright is claimed, and the software is hereby placed in the public
 * domain.
 *
 * Internal header for crypt_gensalt.c — declares the three traditional
 * gensalt functions so call sites can verify signatures match.
 */

#ifndef CRYPT_GENSALT_H
#define CRYPT_GENSALT_H

extern char *_crypt_gensalt_traditional_rn(const char *prefix,
                                           unsigned long count,
                                           const char *input, int size,
                                           char *output, int output_size);

extern char *_crypt_gensalt_extended_rn(const char *prefix,
                                        unsigned long count,
                                        const char *input, int size,
                                        char *output, int output_size);

extern char *_crypt_gensalt_md5_rn(const char *prefix,
                                   unsigned long count,
                                   const char *input, int size,
                                   char *output, int output_size);

#endif /* CRYPT_GENSALT_H */
