// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#include <memory>
#include <quickjs/quickjs.h>
#include <vector>
#define FMT_HEADER_ONLY
#include "util.h"

#include <fmt/format.h>

namespace ccfapp
{
  using namespace std;

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

      // TODO parse algorithm arg

      int extractable = js_get_bool(ctx, argv[3], "extractable");
      if (extractable == -1)
        return js_dump_error(ctx);

      auto key_usages = js_get_string_array(ctx, argv[4], "keyUsages");
      if (!key_usages.has_value())
        return js_dump_error(ctx);

      // TODO import key
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

      if (!JS_IsObject(wrap_algo))
      {
        JS_ThrowTypeError(ctx, "wrapAlgo must be an object");
        return js_dump_error(ctx);
      }

      auto wrap_algo_name = js_get_string_property(ctx, wrap_algo, "name");
      if (!wrap_algo_name.has_value())
        return js_dump_error(ctx);

      if (wrap_algo_name == "RSA-OAEP")
      {
        Buffer label = js_get_array_buffer_property(ctx, wrap_algo, "label");

        // TODO read key to wrap and wrapping key
        // TODO wrap key
      }
      else
      {
        JS_ThrowRangeError(
          ctx,
          "Unsupported wrapAlgo: %s, supported: RSA-OAEP",
          wrap_algo_name.value());
        return js_dump_error(ctx);
      }

      if (format.value() != "pkcs8")
      {
        JS_ThrowRangeError(
          ctx, "Unsupported format: %s, supported: pkcs8", format.value());
        return js_dump_error(ctx);
      }
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
