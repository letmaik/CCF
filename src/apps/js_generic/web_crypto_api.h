// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#include <memory>
#include <quickjs/quickjs.h>
#include <vector>
#define FMT_HEADER_ONLY
#include "tls/key_pair.h"
#include "util.h"

#include <fmt/format.h>

namespace ccfapp
{
  using namespace std;
  using namespace tls;

  JSClassID crypto_key_class_id;
  JSClassDef crypto_key_class_def = {.class_name = "CryptoKey"};

  // TODO maybe a namespace is enough?
  class WebCryptoAPI
  {
  private:
    static void register_class(
      JSRuntime* rt, JSClassID class_id, JSClassDef& class_def)
    {
      auto ret = JS_NewClass(rt, class_id, &class_def);
      if (ret != 0)
      {
        throw std::logic_error(fmt::format(
          "Failed to register JS class definition for {}",
          class_def.class_name));
      }
    }

    static JSValue js_crypto_get_random_values(
      JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
    {
      if (argc != 1)
      {
        JS_ThrowTypeError(ctx, "Passed %d arguments, but expected 1", argc);
        return js_dump_error(ctx);
      }

      Buffer buf = js_get_array_buffer(ctx, argv[0]);
      if (!buf.p)
        return js_dump_error(ctx);

      // TODO fill buf with random values

      return argv[0];
    }

    static JSValue js_subtle_crypto_import_key(
      JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
    {
      if (argc != 5)
      {
        JS_ThrowTypeError(ctx, "Passed %d arguments, but expected 5", argc);
        return js_dump_error(ctx);
      }

      auto format = js_get_string(ctx, argv[0], "format");
      if (!format.has_value())
        return js_dump_error(ctx);

      Buffer key_data = js_get_array_buffer(ctx, argv[1], "keyData");
      if (!key_data.p)
        return js_dump_error(ctx);

      // "algorithm" field of the CryptoKey object.
      // This is a combination of properties parsed from the key and
      // parameters for algorithms that this key is going to be used for.
      // The latter is simply copied from the "algorithm" argument of this
      // function.
      JSValue crypto_key_algorithm = JS_DupValue(ctx, argv[2]);

      // "type" field of the CryptoKey.
      JSValue key_type;

      // mbedtls supports reading but not writing of private keys in PKCS #8
      // format. (https://github.com/ARMmbed/mbedtls/issues/1695) Because we
      // want to support PKCS #8 as export format in wrapKey, we require the key
      // format in importKey to be PKCS #8 as well. This essentially means the
      // input key is not modified and wrapped as-is. If the input key was in
      // JWK format and the wrapKey export format PKCS #8, then we would have to
      // write our own converter to PKCS #8. If the input key was in PKCS #8
      // format and the wrapKey export format JWK, then we would have to write
      // our own converter to JWK, although this would be simpler, given it is
      // just JSON. If the input key format and the wrapKey export format are
      // both JWK, then the same trick as for PKCS #8 can be applied since no
      // conversion is necessary. However, it would still require parsing JWK to
      // fill in the key properties for the "algorithm" field of the CryptoKey
      // object. Given all the above, for now only PKCS #8 private keys are
      // supported.

      std::unique_ptr<mbedtls_pk_context> pk;

      if (format == "pkcs8")
      {
        key_type = JS_NewString(ctx, "private");
        try
        {
          pk = parse_private_key(key_data);
        }
        catch (const std::exception& e)
        {
          JS_ThrowRangeError(ctx, e.what());
          return js_dump_error(ctx);
        }
      }
      else if (format == "spki")
      {
        key_type = JS_NewString(ctx, "public");
        try
        {
          pk = parse_public_key(key_data);
        }
        catch (const std::exception& e)
        {
          JS_ThrowRangeError(ctx, e.what());
          return js_dump_error(ctx);
        }
      }
      else
      {
        JS_ThrowRangeError(
          ctx, "unsupported format: %s, supported: pkcs8, spki", format);
        return js_dump_error(ctx);
      }

      if (mbedtls_pk_get_type(pk.get()) == MBEDTLS_PK_RSA)
      {
        auto algorithm_name =
          js_get_string_property(ctx, crypto_key_algorithm, "name");
        if (!algorithm_name.has_value())
          return js_dump_error(ctx);
        if (
          algorithm_name != "RSASSA-PKCS1-v1_5" &&
          algorithm_name != "RSA-PSS" && algorithm_name != "RSA-OAEP")
        {
          JS_ThrowRangeError(
            ctx,
            "key type is RSA, algorithm.name must be RSASSA-PKCS1-v1_5, "
            "RSA-PSS, or RSA-OAEP");
          return js_dump_error(ctx);
        }

        auto algorithm_hash =
          js_get_string_property(ctx, crypto_key_algorithm, "hash");
        if (!algorithm_hash.has_value())
          return js_dump_error(ctx);
        if (
          algorithm_hash != "SHA-256" && algorithm_hash != "SHA-384" &&
          algorithm_hash != "SHA-512")
        {
          JS_ThrowRangeError(
            ctx,
            "key type is RSA, algorithm.hash must be SHA-256, SHA-384, or "
            "SHA-512");
          return js_dump_error(ctx);
        }

        mbedtls_rsa_context* rsa = mbedtls_pk_rsa(*pk);

        size_t modulus_length = mbedtls_rsa_get_len(rsa) * 8;

        mbedtls_mpi public_exponent_mpi;
        if (
          mbedtls_rsa_export(
            rsa, nullptr, nullptr, nullptr, nullptr, &public_exponent_mpi) != 0)
        {
          JS_ThrowTypeError(ctx, "could not parse RSA parameters");
          return js_dump_error(ctx);
        }
        size_t public_exponent_size = mbedtls_mpi_size(&public_exponent_mpi);
        std::vector<uint8_t> public_exponent(public_exponent_size);
        if (
          mbedtls_mpi_write_binary(
            &public_exponent_mpi,
            public_exponent.data(),
            public_exponent_size) != 0)
        {
          JS_ThrowTypeError(ctx, "could not extract RSA public exponent");
          return js_dump_error(ctx);
        }
        // convert big endian to little endian
        std::reverse(std::begin(public_exponent), std::end(public_exponent));

        JSValue modulus_length_val = JS_NewUint32(ctx, modulus_length);
        JS_SetPropertyStr(
          ctx, crypto_key_algorithm, "modulusLength", modulus_length_val);

        // TODO this should be a Uint8Array, not an ArrayBuffer
        auto public_exponent_val = JS_NewArrayBufferCopy(
          ctx, public_exponent.data(), public_exponent.size());
        JS_SetPropertyStr(
          ctx, crypto_key_algorithm, "publicExponent", public_exponent_val);
      }
      else if (mbedtls_pk_get_type(pk.get()) == MBEDTLS_PK_ECKEY)
      {
        auto algorithm_name =
          js_get_string_property(ctx, crypto_key_algorithm, "name");
        if (!algorithm_name.has_value())
          return js_dump_error(ctx);
        if (algorithm_name != "ECDSA" && algorithm_name != "ECDH")
        {
          JS_ThrowRangeError(
            ctx, "key type is EC, algorithm.name must be ECDSA or ECDH");
          return js_dump_error(ctx);
        }
        auto algorithm_named_curve =
          js_get_string_property(ctx, crypto_key_algorithm, "namedCurve");
        if (!algorithm_named_curve.has_value())
          return js_dump_error(ctx);
        if (
          algorithm_named_curve != "P-256" &&
          algorithm_named_curve != "P-384" && algorithm_named_curve != "P-521")
        {
          JS_ThrowRangeError(
            ctx,
            "key type is EC, algorithm.namedCurve must be P-256, P-384, or "
            "P-521");
          return js_dump_error(ctx);
        }
      }
      else
      {
        JS_ThrowTypeError(ctx, "Unsupported key type, must be RSA or EC");
        return js_dump_error(ctx);
      }

      if (!JS_IsBool(argv[3]))
      {
        JS_ThrowTypeError(ctx, "extractable must be a bool");
        return js_dump_error(ctx);
      }

      auto usages = js_get_string_array(ctx, argv[4], "keyUsages");
      if (!usages.has_value())
        return js_dump_error(ctx);
      if (usages.value().empty())
      {
        JS_ThrowRangeError(ctx, "keyUsages must contain at least one value");
        return js_dump_error(ctx);
      }
      std::set<std::string> allowedKeyUsages{
        "encrypt",
        "decrypt",
        "sign",
        "verify",
        "deriveKey",
        "deriveBits",
        "wrapKey",
        "unwrapKey"};
      for (auto usage : usages.value())
      {
        if (allowedKeyUsages.find(usage) == allowedKeyUsages.end())
        {
          JS_ThrowRangeError(
            ctx, "keyUsages contains an invalid string: %s", usage);
          return js_dump_error(ctx);
        }
      }

      auto key = JS_NewObject(ctx);
      JS_SetPropertyStr(ctx, key, "_keyFormat", argv[0]);
      JS_SetPropertyStr(ctx, key, "_keyData", argv[1]);
      JS_SetPropertyStr(ctx, key, "type", key_type);
      JS_SetPropertyStr(ctx, key, "extractable", argv[3]);
      JS_SetPropertyStr(ctx, key, "algorithm", crypto_key_algorithm);
      JS_SetPropertyStr(ctx, key, "usages", argv[4]);

      return key;
    }

    static JSValue js_subtle_crypto_wrap_key(
      JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
    {
      if (argc != 4)
      {
        JS_ThrowTypeError(ctx, "Passed %d arguments, but expected 4", argc);
        return js_dump_error(ctx);
      }

      auto format = js_get_string(ctx, argv[0], "format");
      if (!format.has_value())
        return js_dump_error(ctx);

      JSValue key = argv[1];
      JSValue wrapping_key = argv[2];
      JSValue wrap_algo = argv[3];

      auto key_format = js_get_string_property(ctx, key, "_keyFormat");
      if (format != key_format)
      {
        // See long comment in js_subtle_crypto_import_key.
        JS_ThrowTypeError(
          ctx, "format argument of wrapKey must match the key's format");
        return js_dump_error(ctx);
      }

      JSValue key_usages_val =
        JS_GetPropertyStr(ctx, wrapping_key, "keyUsages");
      auto key_usages =
        js_get_string_array(ctx, key_usages_val, "wrappingKey.keyUsages");
      if (!key_usages.has_value())
        return js_dump_error(ctx);
      if (
        std::find(
          key_usages.value().begin(), key_usages.value().end(), "wrapKey") ==
        key_usages.value().end())
      {
        JS_ThrowTypeError(ctx, "wrappingkey must have wrapKey usage");
        return js_dump_error(ctx);
      }

      if (!JS_IsObject(wrap_algo))
      {
        JS_ThrowTypeError(ctx, "wrapAlgo must be an object");
        return js_dump_error(ctx);
      }

      auto wrap_algo_name = js_get_string_property(ctx, wrap_algo, "name");
      if (!wrap_algo_name.has_value())
        return js_dump_error(ctx);

      Buffer key_data = js_get_array_buffer_property(ctx, key, "_keyData");
      Buffer wrapping_key_data =
        js_get_array_buffer_property(ctx, wrapping_key, "_keyData");

      if (wrap_algo_name == "RSA-OAEP")
      {
        // https://tools.ietf.org/html/rfc3447#section-7.1

        Buffer label = js_get_array_buffer_property(ctx, wrap_algo, "label");

        // TODO wrap key with mbedtls

      }
      else
      {
        JS_ThrowRangeError(
          ctx,
          "Unsupported wrapAlgo: %s, supported: RSA-OAEP",
          wrap_algo_name.value());
        return js_dump_error(ctx);
      }

      auto wrapped_key_buffer = JS_NewArrayBufferCopy(ctx, wrapped_key_buf, wrapped_key_len);

      return wrapped_key_buffer;
    }

  public:
    void init_rt(JSRuntime* rt)
    {
      register_class(rt, crypto_key_class_id, crypto_key_class_def);
    }

    void init_ctx(JSContext* ctx)
    {
      auto subtle_crypto = JS_NewObject(ctx);
      JS_SetPropertyStr(
        ctx,
        subtle_crypto,
        "importKey",
        JS_NewCFunction(ctx, js_subtle_crypto_import_key, "importKey", 5));
      JS_SetPropertyStr(
        ctx,
        subtle_crypto,
        "wrapKey",
        JS_NewCFunction(ctx, js_subtle_crypto_wrap_key, "wrapKey", 4));

      auto crypto = JS_NewObject(ctx);
      JS_SetPropertyStr(
        ctx,
        crypto,
        "getRandomValues",
        JS_NewCFunction(
          ctx, js_crypto_get_random_values, "getRandomValues", 0));
      JS_SetPropertyStr(ctx, crypto, "subtle", subtle_crypto);

      auto global = JS_GetGlobalObject(ctx);
      JS_SetPropertyStr(ctx, global, "crypto", crypto);
    }
  };

}
