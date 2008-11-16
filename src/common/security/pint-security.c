/* 
 * (C) 2008 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <assert.h>

#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509_vfy.h>

#include "pvfs2.h"
#include "pvfs2-types.h"
#include "pint-eattr.h"
#include "pvfs2-req-proto.h"
#include "pvfs2-internal.h"
#include "gossip.h"
#include "gen-locks.h"
#include "server-config.h"
#include "pint-cached-config.h"
#include "pint-util.h"

#include "pint-security.h"
#include "security-hash.h"
#include "security-util.h"


static gen_mutex_t security_init_mutex = GEN_MUTEX_INITIALIZER;
static gen_mutex_t *openssl_mutexes = NULL;
static int security_init_status = 0;


struct CRYPTO_dynlock_value
{
    gen_mutex_t mutex;
};

static int setup_threading(void);
static void cleanup_threading(void);
static unsigned long id_function(void);
static void locking_function(int, int, const char*, int);
static struct CRYPTO_dynlock_value *dyn_create_function(const char*, int);
static void dyn_lock_function(int, struct CRYPTO_dynlock_value*, const char*,
                              int);
static void dyn_destroy_function(struct CRYPTO_dynlock_value*, const char*,
                                 int);


#ifndef SECURITY_ENCRYPTION_NONE

/* the private key used for signing */
static EVP_PKEY *security_privkey = NULL;
/* the store context used to verify client certificates */
static X509_STORE *security_store = NULL;


static int load_private_key(const char*);
static int load_public_keys(const char*);
static int load_ca_bundle(const char*);
static int verify_callback(int, X509_STORE_CTX*);

#endif /* SECURITY_ENCRYPTION_NONE */


/*  PINT_security_initialize	
 *
 *  Initializes the security module
 *	
 *  returns PVFS_EALREADY if already initialized
 *  returns PVFS_EIO if key file is missing or invalid
 *  returns 0 on sucess
 */
int PINT_security_initialize(void)
{
#ifndef SECURITY_ENCRYPTION_NONE
    const struct server_configuration_s *conf;
#endif /* SECURITY_ENCRYPTION_NONE */
    int ret;

    gen_mutex_lock(&security_init_mutex);
    if (security_init_status)
    {
        gen_mutex_unlock(&security_init_mutex);
        return -PVFS_EALREADY;
    }

    ret = setup_threading();
    if (ret < 0)
    {
        gen_mutex_unlock(&security_init_mutex);
        return ret;
    }

    ERR_load_crypto_strings();
    OpenSSL_add_all_algorithms();

    ret = SECURITY_hash_initialize();
    if (ret < 0)
    {
        EVP_cleanup();
        ERR_free_strings();
        cleanup_threading();
        gen_mutex_unlock(&security_init_mutex);
        return ret;
    }

#ifndef SECURITY_ENCRYPTION_NONE

    conf = PINT_get_server_config();

    assert(conf->serverkey_path);
    security_privkey = EVP_PKEY_new();
    ret = load_private_key(conf->serverkey_path);
    if (ret < 0)
    {
        SECURITY_hash_finalize();
        EVP_cleanup();
        ERR_free_strings();
        cleanup_threading();
        gen_mutex_unlock(&security_init_mutex);
        return -PVFS_EIO;
    }

    assert(conf->keystore_path);
    ret = load_public_keys(conf->keystore_path);
    if (ret < 0)
    {
        EVP_PKEY_free(security_privkey);
        SECURITY_hash_finalize();
        EVP_cleanup();
        ERR_free_strings();
        cleanup_threading();
        gen_mutex_unlock(&security_init_mutex);
        return -PVFS_EIO;
    }

    assert(conf->cabundle_path);
    ret = load_ca_bundle(conf->cabundle_path);
    if (ret < 0)
    {
        EVP_PKEY_free(security_privkey);
        SECURITY_hash_finalize();
        EVP_cleanup();
        ERR_free_strings();
        cleanup_threading();
        gen_mutex_unlock(&security_init_mutex);
        return -PVFS_EIO;
    }

#endif /* SECURITY_ENCRYPTION_NONE */

    security_init_status = 1;
    gen_mutex_unlock(&security_init_mutex);
 
    return 0;
}

/* setup_threading
 * 
 * Sets up the data structures and callbacks required by the OpenSSL library
 * for multithreaded operation.
 *
 * returns -PVFS_ENOMEM if a mutex could not be initialized
 * returns 0 on success
 */
static int setup_threading(void)
{
    int i;

    openssl_mutexes = (gen_mutex_t*)calloc(CRYPTO_num_locks(),
                                           sizeof(gen_mutex_t));
    if (!openssl_mutexes)
    {
        return -PVFS_ENOMEM;
    }

    for (i = 0; i < CRYPTO_num_locks(); i++)
    {
        if (gen_mutex_init(&openssl_mutexes[i]) != 0)
        {
            for (i = i - 1; i >= 0; i--)
            {
                gen_mutex_destroy(&openssl_mutexes[i]);
            }
            free(openssl_mutexes);
            openssl_mutexes = NULL;
            return -PVFS_ENOMEM;
        }
    }

    CRYPTO_set_id_callback(id_function);
    CRYPTO_set_locking_callback(locking_function);
    CRYPTO_set_dynlock_create_callback(dyn_create_function);
    CRYPTO_set_dynlock_lock_callback(dyn_lock_function);
    CRYPTO_set_dynlock_destroy_callback(dyn_destroy_function);

    return 0;
}

/* id_function
 *
 * The OpenSSL id_function callback.
 */
static unsigned long id_function(void)
{
    return (unsigned long)gen_thread_self();
}

/* locking_function
 *
 * The OpenSSL locking_function callback.
 */
static void locking_function(int mode, int n, const char *file, int line)
{
    if (mode & CRYPTO_LOCK)
    {
        gen_mutex_lock(&openssl_mutexes[n]);
    }
    else
    {
        gen_mutex_unlock(&openssl_mutexes[n]);
    }
}

/* dyn_create_function
 *
 * The OpenSSL dyn_create_function callback.
 */
static struct CRYPTO_dynlock_value *dyn_create_function(const char *file, 
                                                        int line)
{
    struct CRYPTO_dynlock_value *ret;

    ret = (struct CRYPTO_dynlock_value*)malloc(sizeof(struct CRYPTO_dynlock_value));
    if (ret)
    {
        gen_mutex_init(&ret->mutex);
    }

    return ret;
}

/* dyn_lock_function
 *
 * The OpenSSL dyn_lock_function callback.
 */
static void dyn_lock_function(int mode, struct CRYPTO_dynlock_value *l,
                              const char *file, int line)
{
    if (mode & CRYPTO_LOCK)
    {
        gen_mutex_lock(&l->mutex);
    }
    else
    {
        gen_mutex_unlock(&l->mutex);
    }
}

/* dyn_destroy_function
 *
 * The OpenSSL dyn_destroy_function callback.
 */
static void dyn_destroy_function(struct CRYPTO_dynlock_value *l,
                                 const char *file, int line)
{
    gen_mutex_destroy(&l->mutex);
    free(l);
}

/* cleanup_threading
 *
 * Frees the data structures used to provide multithreading support to
 * the OpenSSL library.
 */
static void cleanup_threading(void)
{
    int i;

    CRYPTO_set_id_callback(NULL);
    CRYPTO_set_locking_callback(NULL);
    CRYPTO_set_dynlock_create_callback(NULL);
    CRYPTO_set_dynlock_lock_callback(NULL);
    CRYPTO_set_dynlock_destroy_callback(NULL);
    
    if (openssl_mutexes)
    {
        for (i = 0; i < CRYPTO_num_locks(); i++)
        {
            gen_mutex_destroy(&openssl_mutexes[i]);
        }
    }

    free(openssl_mutexes);
    openssl_mutexes = NULL;
}

/*  PINT_security_finalize	
 *
 *  Finalizes the security module
 *	
 *  returns PVFS_EALREADY if already finalized
 *  returns 0 on sucess
 */
int PINT_security_finalize(void)
{
    gen_mutex_lock(&security_init_mutex);
    if (!security_init_status)
    {
        gen_mutex_unlock(&security_init_mutex);
        return -PVFS_EALREADY;
    }

    SECURITY_hash_finalize();

#ifndef SECURITY_ENCRYPTION_NONE
    EVP_PKEY_free(security_privkey);
    X509_STORE_free(security_store);
#endif

    EVP_cleanup();
    ERR_free_strings();
    cleanup_threading();

    security_init_status = 0;
    gen_mutex_unlock(&security_init_mutex);
    
    return 0;
}

#ifndef SECURITY_ENCRYPTION_NONE

/*  PINT_sign_capability
 *
 *  Takes in a PVFS_capability structure and creates a signature
 *  based on the input data
 *
 *  returns 0 on success
 *  returns -1 on error
 */
int PINT_sign_capability(PVFS_capability *cap)
{
    const struct server_configuration_s *conf;
    EVP_MD_CTX mdctx;
    char buf[256];
    const EVP_MD *md;
    int ret;

    assert(security_privkey);

    conf = PINT_get_server_config();

    cap->timeout = PINT_util_get_current_time() + conf->security_timeout;

#if defined(SECURITY_ENCRYPTION_RSA)
    md = EVP_sha1();
#elif defined(SECURITY_ENCRYPTION_DSA)
    md = EVP_dss1();
#endif

    EVP_MD_CTX_init(&mdctx);

    ret = EVP_SignInit_ex(&mdctx, md, NULL);
    if (!ret)
    {
        gossip_debug(GOSSIP_SECURITY_DEBUG, "Error signing capability: "
                         "%s\n", ERR_error_string(ERR_get_error(), buf));
        EVP_MD_CTX_cleanup(&mdctx);
        return -1;
    }

    ret = EVP_SignUpdate(&mdctx, &cap->owner, sizeof(PVFS_handle));
    ret &= EVP_SignUpdate(&mdctx, &cap->fsid, sizeof(PVFS_fs_id));
    ret &= EVP_SignUpdate(&mdctx, &cap->timeout, sizeof(PVFS_time));
    ret &= EVP_SignUpdate(&mdctx, &cap->op_mask, sizeof(uint32_t));
    ret &= EVP_SignUpdate(&mdctx, &cap->num_handles, sizeof(uint32_t));
    if (cap->num_handles)
    {
        ret &= EVP_SignUpdate(&mdctx, cap->handle_array, cap->num_handles * 
                          sizeof(PVFS_handle));
    }

    if (!ret)
    {
        gossip_debug(GOSSIP_SECURITY_DEBUG, "Error signing capability: "
                         "%s\n", ERR_error_string(ERR_get_error(), buf));
        EVP_MD_CTX_cleanup(&mdctx);
        return -1;
    }

    ret = EVP_SignFinal(&mdctx, cap->signature, &cap->sig_size, 
                        security_privkey);
    if (!ret)
    {
        gossip_debug(GOSSIP_SECURITY_DEBUG, "Error signing capability: "
                         "%s\n", ERR_error_string(ERR_get_error(), buf));
        EVP_MD_CTX_cleanup(&mdctx);
        return -1;
    }

    EVP_MD_CTX_cleanup(&mdctx);

    return 0;
}

/*  PINT_verify_capability
 *
 *  Takes in a PVFS_capability structure and checks to see if the
 *  signature matches the contents based on the data within. Verification
 *  always succeeds for the null capability.
 *
 *  returns 1 on success
 *  returns 0 on error or failure to verify
 */
int PINT_verify_capability(PVFS_capability *data)
{
    EVP_MD_CTX mdctx;
    const EVP_MD *md;
    int ret;
    char *bmi, *alias;
    EVP_PKEY *pubkey;
    
    if (!data)
    {
        return 0;
    }

    if (PINT_capability_is_null(data))
    {
        return 1;
    }

    if (PINT_util_get_current_time() > data->timeout)
    {
        return 0;
    }
    
    bmi = (char*)malloc(sizeof(char) * 1024);    
    if (bmi == NULL)
    {
        return 0;
    }

    ret = PINT_cached_config_get_server_name(bmi, 1024, data->handle_array[0],
                                             data->fsid);
    if (ret < 0)
    {
        gossip_debug(GOSSIP_SECURITY_DEBUG, "Server name lookup failed.\n");
        free(bmi);
        return 0;
    }

    alias = PINT_config_get_host_alias_ptr(PINT_get_server_config(), bmi);
    if (!alias)
    {
        gossip_err("BMI to host alias mapping failed.\n");
        free(bmi);
        return 0;
    }   

    pubkey = SECURITY_lookup_pubkey(alias);
    if (pubkey == NULL)
    {
        gossip_debug(GOSSIP_SECURITY_DEBUG,
                     "Public key not found in lookup. Name used: '%s'\n", 
                     alias);
        free(bmi);
        return 0;
    }
    free(bmi);

#if defined(SECURITY_ENCRYPTION_RSA)
    md = EVP_sha1();
#elif defined(SECURITY_ENCRYPTION_DSA)
    md = EVP_dss1();
#endif

    EVP_MD_CTX_init(&mdctx);
    ret = EVP_VerifyInit_ex(&mdctx, md, NULL);
    if (ret)
    {
        ret = EVP_VerifyUpdate(&mdctx, &(data->owner), sizeof(PVFS_handle));
        ret &= EVP_VerifyUpdate(&mdctx, &(data->fsid), sizeof(PVFS_fs_id));
        ret &= EVP_VerifyUpdate(&mdctx, &(data->timeout), sizeof(PVFS_time));
        ret &= EVP_VerifyUpdate(&mdctx, &(data->op_mask), sizeof(uint32_t));
        ret &= EVP_VerifyUpdate(&mdctx, &(data->num_handles),
                                sizeof(uint32_t));
        if (data->num_handles)
        {
            ret &= EVP_VerifyUpdate(&mdctx, data->handle_array,
                                sizeof(PVFS_handle) * data->num_handles);
        }
        if (ret)
        {
            ret = EVP_VerifyFinal(&mdctx, data->signature, data->sig_size, 
                                  pubkey);
        }
        else 
        {
            gossip_debug(GOSSIP_SECURITY_DEBUG, "VerifyUpdate failure.\n");
            EVP_MD_CTX_cleanup(&mdctx);
            return 0;
        }
    }
    else
    {
        gossip_debug(GOSSIP_SECURITY_DEBUG, "VerifyInit failure.\n");
        EVP_MD_CTX_cleanup(&mdctx);
        return 0;
    }
    
    EVP_MD_CTX_cleanup(&mdctx);

    return (ret == 1);
}

/*  PINT_verify_credential
 *
 *  Takes in a PVFS_credential structure and checks to see if the
 *  signature matches the contents based on the data within
 *
 *  returns 1 on success
 *  returns 0 on error or failure to verify
 */
int PINT_verify_credential(PVFS_credential *cred)
{
    EVP_MD_CTX mdctx;
    const EVP_MD *md;
    EVP_PKEY *pubkey;
    int ret;

    if (!cred)
    {
        return 0;
    }

    if (PINT_util_get_current_time() > cred->timeout)
    {
        return 0;
    }

    /* TODO: check revocation list against cred->serial */

    pubkey = SECURITY_lookup_pubkey(cred->issuer_id);
    if (pubkey == NULL)
    {
        gossip_debug(GOSSIP_SECURITY_DEBUG,
                     "Public key not found for issuer: %s\n", 
                     cred->issuer_id);
        return 0;
    }

#if defined(SECURITY_ENCRYPTION_RSA)
    md = EVP_sha1();
#elif defined(SECURITY_ENCRYPTION_DSA)
    md = EVP_dss1();
#endif

    EVP_MD_CTX_init(&mdctx);
    ret = EVP_VerifyInit_ex(&mdctx, md, NULL);
    if (!ret)
    {
        gossip_debug(GOSSIP_SECURITY_DEBUG, "VerifyInit failure.\n");
        EVP_MD_CTX_cleanup(&mdctx);
        return 0;
    }

    ret = EVP_VerifyUpdate(&mdctx, &cred->serial, sizeof(uint32_t));
    ret &= EVP_VerifyUpdate(&mdctx, &cred->userid, sizeof(PVFS_uid));
    ret &= EVP_VerifyUpdate(&mdctx, &cred->num_groups, sizeof(uint32_t));
    if (cred->num_groups)
    {
        ret &= EVP_VerifyUpdate(&mdctx, cred->group_array,
                                cred->num_groups * sizeof(PVFS_gid));
    }
    ret &= EVP_VerifyUpdate(&mdctx, cred->issuer_id,
                            (strlen(cred->issuer_id) + 1) * sizeof(char));
    ret &= EVP_VerifyUpdate(&mdctx, &cred->timeout, sizeof(PVFS_time));
    ret &= EVP_VerifyUpdate(&mdctx, &cred->sig_size, sizeof(uint32_t));
    if (!ret)
    {
        gossip_debug(GOSSIP_SECURITY_DEBUG, "VerifyUpdate failure.\n");
        EVP_MD_CTX_cleanup(&mdctx);
        return 0;
    }

    ret = EVP_VerifyFinal(&mdctx, cred->signature, cred->sig_size, pubkey);

    EVP_MD_CTX_cleanup(&mdctx);

    return (ret == 1);
}

/* load_private_key
 *
 * Reads the private key from a file in PEM format.
 *
 * returns -1 on error
 * returns 0 on success
 */
static int load_private_key(const char *path)
{
    FILE *keyfile;
    char buf[256];

    keyfile = fopen(path, "r");
    if (keyfile == NULL)
    {
        gossip_err("%s: %s\n", path, strerror(errno));
        return -1;
    }

    EVP_PKEY_free(security_privkey);
    security_privkey = PEM_read_PrivateKey(keyfile, NULL, NULL, NULL);
    if (security_privkey == NULL)
    {
        gossip_debug(GOSSIP_SECURITY_DEBUG, "Error loading private key: "
                         "%s\n", ERR_error_string(ERR_get_error(), buf));
        fclose(keyfile);
        return -1;
    }

    fclose(keyfile);

    return 0;
}

/*  load_public_keys
 *
 *  Internal function to load keys from a file.
 *  File path includes the filename
 *  When finished without error, hash table will be filled
 *  with all host ID / public key pairs.
 *	
 *  returns -1 on error
 *  returns 0 on success
 */
static int load_public_keys(const char *path)
{
    FILE *keyfile;
    int ch, ptr;
    static char buf[1024];
    EVP_PKEY *key;
    int ret;

    keyfile = fopen(path, "r");
    if (keyfile == NULL)
    {
        gossip_err("%s: %s\n", path, strerror(errno));
        return -1;
    }

    ch = fgetc(keyfile);

    while (ch != EOF)
    {
        while (isspace(ch))
        {
            ch = fgetc(keyfile);
        }

        if (ch == EOF)
        {
            break;
        }

        if (!isalnum(ch))
        {
            fclose(keyfile);
            return -1;
        }

        for (ptr = 0; (ptr < 1023) && isalnum(ch); ptr++)
        {
            buf[ptr] = (char)ch;
            ch = fgetc(keyfile);
            if (ch == EOF)
            {
                fclose(keyfile);
                return -1;
            }
        }
        buf[ptr] = '\0';

        do
        {
            ch = fgetc(keyfile);
        } while(isspace(ch));

        ungetc(ch, keyfile);

        key = PEM_read_PUBKEY(keyfile, NULL, NULL, NULL);
        if (key == NULL)
        {
            gossip_debug(GOSSIP_SECURITY_DEBUG, "Error loading public key: "
                         "%s\n", ERR_error_string(ERR_get_error(), buf));
            fclose(keyfile);
            return -1;
        }

        ret = SECURITY_add_pubkey(buf, key);
        if (ret < 0)
        {
            PVFS_strerror_r(ret, buf, 1024);
            gossip_debug(GOSSIP_SECURITY_DEBUG, "Error inserting public "
                         "key: %s\n", buf);
            fclose(keyfile);
            return -1;
        }
        gossip_debug(GOSSIP_SECURITY_DEBUG, 
                     "Added public key for '%s'\n", 
                     buf);
        
        ch = fgetc(keyfile);
    }

    fclose(keyfile);

    return 0;
}

/* load_ca_bundle
 * TODO: commment me
 */
static int load_ca_bundle(const char *path)
{
    static char buf[320];
    int ret;

    security_store = X509_STORE_new();
    if (!security_store)
    {
        ERR_error_string_n(ERR_get_error(), buf, 320);
        gossip_debug(GOSSIP_SECURITY_DEBUG, "Error creating security store: "
                     "%s\n", buf);
        gossip_debug(GOSSIP_SECURITY_DEBUG, "ok");
        return -1;
    }

    X509_STORE_set_verify_cb_func(security_store, verify_callback);

    ret = X509_STORE_load_locations(security_store, path, NULL);
    if (!ret)
    {
        ERR_error_string_n(ERR_get_error(), buf, 320);
        gossip_debug(GOSSIP_SECURITY_DEBUG, "Error loading CA bundle file: "
                     "%s\n", buf);
        X509_STORE_free(security_store);
        return -1;
    }

    return 0;
}

/* TODO: implement me */
static int verify_callback(int ok, X509_STORE_CTX *ctx)
{
    return ok;
}

#else /* SECURITY_ENCRYPTION_NONE */

/*  PINT_sign_capability
 *
 *  placeholder for when security is disabled, sets sig size to zero
 */
int PINT_sign_capability(PVFS_capability *cap)
{
    cap->sig_size = 0;

    return 0;
}

/*  PINT_verify_capability
 *
 *  placeholder for when security is disabled, always verifies
 */
int PINT_verify_capability(PVFS_capability *cap)
{
    int ret;
    
    if (PINT_util_get_current_time() > cap->timeout)
    {
        ret = 0;
    }
    else
    {
        ret = 1;
    }

    return ret;
}

/*  PINT_verify_credential
 *
 *  placeholder for when security is disabled, always verifies
 */
int PINT_verify_credential(PVFS_credential *cred)
{
    int ret;
    
    if (PINT_util_get_current_time() > cred->timeout)
    {
        ret = 0;
    }
    else
    {
        ret = 1;
    }

    return ret;
}

#endif /* SECURITY_ENCRYPTION_NONE */

/*  PINT_init_capability
 *
 *  Function to call after creating an initial capability
 *  structure to initialize needed memory space for the signature.
 *  Sets all fields to 0 or NULL to be safe
 *	
 *  returns -PVFS_ENOMEM on error
 *  returns -PVFS_EINVAL if passed an invalid structure
 *  returns 0 on success
 */
int PINT_init_capability(PVFS_capability *cap)
{
    int ret = 0;
    
    if (cap)
    {
        memset(cap, 0, sizeof(PVFS_capability));

#ifndef SECURITY_ENCRYPTION_NONE
        cap->signature = 
            (unsigned char*)calloc(1, EVP_PKEY_size(security_privkey));
        if (cap->signature == NULL)
        {
            ret = -PVFS_ENOMEM;
        }
#endif /* SECURITY_ENCRYPTION_NONE */
    }
    else
    {
        ret = -PVFS_EINVAL;
    }

    return ret;
}


/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
