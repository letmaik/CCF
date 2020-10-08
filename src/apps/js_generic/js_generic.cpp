// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#include "enclave/app_interface.h"
#include "kv/untyped_map.h"
#include "node/rpc/user_frontend.h"
#include "util.h"

#include <memory>
#include <quickjs/quickjs-exports.h>
#include <quickjs/quickjs.h>
#include <vector>

namespace ccfapp
{
  using namespace std;
  using namespace kv;
  using namespace ccf;

  using KVMap = kv::Map<std::vector<uint8_t>, std::vector<uint8_t>>;

  JSClassID kv_class_id;
  JSClassID kv_map_view_class_id;
  JSClassID body_class_id;

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc99-extensions"

  static void js_free_arraybuffer_cstring(JSRuntime*, void* opaque, void* ptr)
  {
    JS_FreeCString((JSContext*)opaque, (char*)ptr);
  }

  static JSValue js_str_to_buf(
    JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
  {
    if (argc != 1)
      return JS_ThrowTypeError(
        ctx, "Passed %d arguments, but expected 1", argc);

    if (!JS_IsString(argv[0]))
      return JS_ThrowTypeError(ctx, "Argument must be a string");

    size_t str_size = 0;
    const char* str = JS_ToCStringLen(ctx, &str_size, argv[0]);

    if (!str)
    {
      js_dump_error(ctx);
      return JS_EXCEPTION;
    }

    JSValue buf = JS_NewArrayBuffer(
      ctx, (uint8_t*)str, str_size, js_free_arraybuffer_cstring, ctx, false);

    if (JS_IsException(buf))
      js_dump_error(ctx);

    return buf;
  }

  static JSValue js_buf_to_str(
    JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
  {
    if (argc != 1)
      return JS_ThrowTypeError(
        ctx, "Passed %d arguments, but expected 1", argc);

    size_t buf_size;
    uint8_t* buf = JS_GetArrayBuffer(ctx, &buf_size, argv[0]);

    if (!buf)
      return JS_ThrowTypeError(ctx, "Argument must be an ArrayBuffer");

    JSValue str = JS_NewStringLen(ctx, (char*)buf, buf_size);

    if (JS_IsException(str))
      js_dump_error(ctx);

    return str;
  }

  static JSValue js_json_compatible_to_buf(
    JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
  {
    if (argc != 1)
      return JS_ThrowTypeError(
        ctx, "Passed %d arguments, but expected 1", argc);

    JSValue str = JS_JSONStringify(ctx, argv[0], JS_NULL, JS_NULL);

    if (JS_IsException(str))
    {
      js_dump_error(ctx);
      return str;
    }

    JSValue buf = js_str_to_buf(ctx, JS_NULL, 1, &str);
    JS_FreeValue(ctx, str);
    return buf;
  }

  static JSValue js_buf_to_json_compatible(
    JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
  {
    if (argc != 1)
      return JS_ThrowTypeError(
        ctx, "Passed %d arguments, but expected 1", argc);

    size_t buf_size;
    uint8_t* buf = JS_GetArrayBuffer(ctx, &buf_size, argv[0]);

    if (!buf)
      return JS_ThrowTypeError(ctx, "Argument must be an ArrayBuffer");

    std::vector<uint8_t> buf_null_terminated(buf_size + 1);
    buf_null_terminated[buf_size] = 0;
    buf_null_terminated.assign(buf, buf + buf_size);

    JSValue obj =
      JS_ParseJSON(ctx, (char*)buf_null_terminated.data(), buf_size, "<json>");

    if (JS_IsException(obj))
      js_dump_error(ctx);

    return obj;
  }

  static JSValue js_kv_map_get(
    JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
  {
    auto map_view =
      static_cast<KVMap::TxView*>(JS_GetOpaque(this_val, kv_map_view_class_id));

    if (argc != 1)
      return JS_ThrowTypeError(
        ctx, "Passed %d arguments, but expected 1", argc);

    size_t key_size;
    uint8_t* key = JS_GetArrayBuffer(ctx, &key_size, argv[0]);

    if (!key)
      return JS_ThrowTypeError(ctx, "Argument must be an ArrayBuffer");

    auto val = map_view->get({key, key + key_size});

    if (!val.has_value())
      return JS_ThrowRangeError(ctx, "No such key");

    JSValue buf =
      JS_NewArrayBufferCopy(ctx, val.value().data(), val.value().size());

    if (JS_IsException(buf))
      js_dump_error(ctx);

    return buf;
  }

  static JSValue js_kv_map_delete(
    JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
  {
    auto map_view =
      static_cast<KVMap::TxView*>(JS_GetOpaque(this_val, kv_map_view_class_id));

    if (argc != 1)
      return JS_ThrowTypeError(
        ctx, "Passed %d arguments, but expected 1", argc);

    size_t key_size;
    uint8_t* key = JS_GetArrayBuffer(ctx, &key_size, argv[0]);

    if (!key)
      return JS_ThrowTypeError(ctx, "Argument must be an ArrayBuffer");

    auto val = map_view->remove({key, key + key_size});

    if (!val)
      return JS_ThrowRangeError(ctx, "Failed to remove at key");

    return JS_UNDEFINED;
  }

  static JSValue js_kv_map_set(
    JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
  {
    auto map_view =
      static_cast<KVMap::TxView*>(JS_GetOpaque(this_val, kv_map_view_class_id));

    if (argc != 2)
      return JS_ThrowTypeError(
        ctx, "Passed %d arguments, but expected 2", argc);

    size_t key_size;
    uint8_t* key = JS_GetArrayBuffer(ctx, &key_size, argv[0]);

    size_t val_size;
    uint8_t* val = JS_GetArrayBuffer(ctx, &val_size, argv[1]);

    if (!key || !val)
      return JS_ThrowTypeError(ctx, "Arguments must be ArrayBuffers");

    if (!map_view->put({key, key + key_size}, {val, val + val_size}))
      return JS_ThrowRangeError(ctx, "Could not insert at key");

    return JS_UNDEFINED;
  }

  static int js_kv_lookup(
    JSContext* ctx,
    JSPropertyDescriptor* desc,
    JSValueConst this_val,
    JSAtom property)
  {
    const auto property_name = JS_AtomToCString(ctx, property);
    LOG_TRACE_FMT("Looking for kv map '{}'", property_name);

    auto tx_ptr = static_cast<kv::Tx*>(JS_GetOpaque(this_val, kv_class_id));
    auto view = tx_ptr->get_view<KVMap>(property_name);

    // This follows the interface of Map:
    // https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Map
    // Keys and values are ArrayBuffers. Keys are matched based on their
    // contents.
    auto view_val = JS_NewObjectClass(ctx, kv_map_view_class_id);
    JS_SetOpaque(view_val, view);

    JS_SetPropertyStr(
      ctx,
      view_val,
      "get",
      JS_NewCFunction(ctx, ccfapp::js_kv_map_get, "get", 1));
    JS_SetPropertyStr(
      ctx,
      view_val,
      "set",
      JS_NewCFunction(ctx, ccfapp::js_kv_map_set, "set", 2));
    JS_SetPropertyStr(
      ctx,
      view_val,
      "delete",
      JS_NewCFunction(ctx, ccfapp::js_kv_map_delete, "delete", 1));

    desc->flags = 0;
    desc->value = view_val;

    return true;
  }

  static JSValue js_body_text(
    JSContext* ctx,
    JSValueConst this_val,
    int argc,
    [[maybe_unused]] JSValueConst* argv)
  {
    if (argc != 0)
      return JS_ThrowTypeError(
        ctx, "Passed %d arguments, but expected none", argc);

    auto body = static_cast<const std::vector<uint8_t>*>(
      JS_GetOpaque(this_val, body_class_id));
    auto body_ = JS_NewStringLen(ctx, (const char*)body->data(), body->size());
    return body_;
  }

  static JSValue js_body_json(
    JSContext* ctx,
    JSValueConst this_val,
    int argc,
    [[maybe_unused]] JSValueConst* argv)
  {
    if (argc != 0)
      return JS_ThrowTypeError(
        ctx, "Passed %d arguments, but expected none", argc);

    auto body = static_cast<const std::vector<uint8_t>*>(
      JS_GetOpaque(this_val, body_class_id));
    std::string body_str(body->begin(), body->end());
    auto body_ = JS_ParseJSON(ctx, body_str.c_str(), body->size(), "<body>");
    return body_;
  }

  static JSValue js_body_array_buffer(
    JSContext* ctx,
    JSValueConst this_val,
    int argc,
    [[maybe_unused]] JSValueConst* argv)
  {
    if (argc != 0)
      return JS_ThrowTypeError(
        ctx, "Passed %d arguments, but expected none", argc);

    auto body = static_cast<const std::vector<uint8_t>*>(
      JS_GetOpaque(this_val, body_class_id));
    auto body_ = JS_NewArrayBufferCopy(ctx, body->data(), body->size());
    return body_;
  }

  // Partially replicates https://developer.mozilla.org/en-US/docs/Web/API/Body
  // with a synchronous interface.
  static const JSCFunctionListEntry js_body_proto_funcs[] = {
    JS_CFUNC_DEF("text", 0, js_body_text),
    JS_CFUNC_DEF("json", 0, js_body_json),
    JS_CFUNC_DEF("arrayBuffer", 0, js_body_array_buffer),
  };

  struct JSModuleLoaderArg
  {
    ccf::NetworkTables* network;
    kv::Tx* tx;
  };

  static JSModuleDef* js_module_loader(
    JSContext* ctx, const char* module_name, void* opaque)
  {
    // QuickJS resolves relative paths but in some cases omits leading slashes.
    std::string module_name_kv(module_name);
    if (module_name_kv[0] != '/')
    {
      module_name_kv.insert(0, "/");
    }

    LOG_TRACE_FMT("Loading module '{}'", module_name_kv);

    auto arg = (JSModuleLoaderArg*)opaque;

    const auto modules = arg->tx->get_view(arg->network->modules);
    auto module = modules->get(module_name_kv);
    if (!module.has_value())
    {
      JS_ThrowReferenceError(ctx, "module '%s' not found in kv", module_name);
      return nullptr;
    }
    std::string js = module->js;

    const char* buf = js.c_str();
    size_t buf_len = js.size();
    JSValue func_val = JS_Eval(
      ctx,
      buf,
      buf_len,
      module_name,
      JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
    if (JS_IsException(func_val))
    {
      js_dump_error(ctx);
      return nullptr;
    }

    auto m = (JSModuleDef*)JS_VALUE_GET_PTR(func_val);
    // module already referenced, decrement ref count
    JS_FreeValue(ctx, func_val);
    return m;
  }

  class JSHandlers : public UserEndpointRegistry
  {
  private:
    NetworkTables& network;

    JSClassDef kv_class_def = {};
    JSClassExoticMethods kv_exotic_methods = {};

    JSClassDef kv_map_view_class_def = {};

    JSClassDef body_class_def = {};

  public:
    JSHandlers(NetworkTables& network) :
      UserEndpointRegistry(network),
      network(network)
    {
      JS_NewClassID(&kv_class_id);
      kv_exotic_methods.get_own_property = js_kv_lookup;
      kv_class_def.class_name = "KV";
      kv_class_def.exotic = &kv_exotic_methods;

      JS_NewClassID(&kv_map_view_class_id);
      kv_map_view_class_def.class_name = "KVMap";

      JS_NewClassID(&body_class_id);
      body_class_def.class_name = "Body";

      auto default_handler = [this](EndpointContext& args) {
        const auto method = args.rpc_ctx->get_method();
        const auto local_method = method.substr(method.find_first_not_of('/'));

        const auto scripts = args.tx.get_view(this->network.app_scripts);

        // Try to find script for method
        // - First try a script called "foo"
        // - If that fails, try a script called "POST foo"
        auto handler_script = scripts->get(local_method);
        if (!handler_script)
        {
          const auto verb_prefixed = fmt::format(
            "{} {}", args.rpc_ctx->get_request_verb().c_str(), local_method);
          handler_script = scripts->get(verb_prefixed);
          if (!handler_script)
          {
            args.rpc_ctx->set_response_status(HTTP_STATUS_NOT_FOUND);
            args.rpc_ctx->set_response_body(fmt::format(
              "No handler script found for method '{}'", verb_prefixed));
            return;
          }
        }

        JSRuntime* rt = JS_NewRuntime();
        if (rt == nullptr)
        {
          throw std::runtime_error("Failed to initialise QuickJS runtime");
        }

        JS_SetMaxStackSize(rt, 1024 * 1024);

        JSModuleLoaderArg js_module_loader_arg{&this->network, &args.tx};
        JS_SetModuleLoaderFunc(
          rt, nullptr, js_module_loader, &js_module_loader_arg);

        JSContext* ctx = JS_NewContext(rt);
        if (ctx == nullptr)
        {
          JS_FreeRuntime(rt);
          throw std::runtime_error("Failed to initialise QuickJS context");
        }

        // Register class for KV
        {
          auto ret = JS_NewClass(rt, kv_class_id, &kv_class_def);
          if (ret != 0)
          {
            throw std::logic_error(
              "Failed to register JS class definition for KV");
          }
        }

        // Register class for KV map views
        {
          auto ret =
            JS_NewClass(rt, kv_map_view_class_id, &kv_map_view_class_def);
          if (ret != 0)
          {
            throw std::logic_error(
              "Failed to register JS class definition for KVMap");
          }
        }

        // Register class for request body
        {
          auto ret = JS_NewClass(rt, body_class_id, &body_class_def);
          if (ret != 0)
          {
            throw std::logic_error(
              "Failed to register JS class definition for Body");
          }
          JSValue body_proto = JS_NewObject(ctx);
          size_t func_count =
            sizeof(js_body_proto_funcs) / sizeof(js_body_proto_funcs[0]);
          JS_SetPropertyFunctionList(
            ctx, body_proto, js_body_proto_funcs, func_count);
          JS_SetClassProto(ctx, body_class_id, body_proto);
        }

        auto global_obj = JS_GetGlobalObject(ctx);

        auto console = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, global_obj, "console", console);

        JS_SetPropertyStr(
          ctx,
          console,
          "log",
          JS_NewCFunction(ctx, ccfapp::js_print, "log", 1));

        auto ccf = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, global_obj, "ccf", ccf);

        JS_SetPropertyStr(
          ctx,
          ccf,
          "strToBuf",
          JS_NewCFunction(ctx, ccfapp::js_str_to_buf, "strToBuf", 1));
        JS_SetPropertyStr(
          ctx,
          ccf,
          "bufToStr",
          JS_NewCFunction(ctx, ccfapp::js_buf_to_str, "bufToStr", 1));
        JS_SetPropertyStr(
          ctx,
          ccf,
          "jsonCompatibleToBuf",
          JS_NewCFunction(
            ctx, ccfapp::js_json_compatible_to_buf, "jsonCompatibleToBuf", 1));
        JS_SetPropertyStr(
          ctx,
          ccf,
          "bufToJsonCompatible",
          JS_NewCFunction(
            ctx, ccfapp::js_buf_to_json_compatible, "bufToJsonCompatible", 1));

        auto kv = JS_NewObjectClass(ctx, kv_class_id);
        JS_SetPropertyStr(ctx, ccf, "kv", kv);
        JS_SetOpaque(kv, &args.tx);

        auto request = JS_NewObject(ctx);

        auto headers = JS_NewObject(ctx);
        for (auto& [header_name, header_value] :
             args.rpc_ctx->get_request_headers())
        {
          JS_SetPropertyStr(
            ctx,
            headers,
            header_name.c_str(),
            JS_NewStringLen(ctx, header_value.c_str(), header_value.size()));
        }
        JS_SetPropertyStr(ctx, request, "headers", headers);

        const auto& request_query = args.rpc_ctx->get_request_query();
        auto query_str =
          JS_NewStringLen(ctx, request_query.c_str(), request_query.size());
        JS_SetPropertyStr(ctx, request, "query", query_str);

        auto params = JS_NewObject(ctx);
        for (auto& [param_name, param_value] :
             args.rpc_ctx->get_request_path_params())
        {
          JS_SetPropertyStr(
            ctx,
            params,
            param_name.c_str(),
            JS_NewStringLen(ctx, param_value.c_str(), param_value.size()));
        }
        JS_SetPropertyStr(ctx, request, "params", params);

        const auto& request_body = args.rpc_ctx->get_request_body();
        auto body_ = JS_NewObjectClass(ctx, body_class_id);
        JS_SetOpaque(body_, (void*)&request_body);
        JS_SetPropertyStr(ctx, request, "body", body_);

        JS_FreeValue(ctx, global_obj);

        if (!handler_script.value().text.has_value())
        {
          throw std::runtime_error("Could not find script text");
        }

        // Compile module
        std::string code = handler_script.value().text.value();
        const std::string path = "/__endpoint__.js";
        JSValue module = JS_Eval(
          ctx,
          code.c_str(),
          code.size(),
          path.c_str(),
          JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);

        if (JS_IsException(module))
        {
          js_dump_error(ctx);
          args.rpc_ctx->set_response_status(HTTP_STATUS_INTERNAL_SERVER_ERROR);
          args.rpc_ctx->set_response_body("Exception thrown while compiling");
          return;
        }

        // Evaluate module
        auto eval_val = JS_EvalFunction(ctx, module);
        if (JS_IsException(eval_val))
        {
          js_dump_error(ctx);
          args.rpc_ctx->set_response_status(HTTP_STATUS_INTERNAL_SERVER_ERROR);
          args.rpc_ctx->set_response_body("Exception thrown while executing");
          return;
        }
        JS_FreeValue(ctx, eval_val);

        // Get exported function from module
        assert(JS_VALUE_GET_TAG(module) == JS_TAG_MODULE);
        auto module_def = (JSModuleDef*)JS_VALUE_GET_PTR(module);
        if (JS_GetModuleExportEntriesCount(module_def) != 1)
        {
          throw std::runtime_error(
            "Endpoint module exports more than one function");
        }
        auto export_func = JS_GetModuleExportEntry(ctx, module_def, 0);
        if (!JS_IsFunction(ctx, export_func))
        {
          throw std::runtime_error(
            "Endpoint module exports something that is not a function");
        }

        // Call exported function
        int argc = 1;
        JSValueConst* argv = (JSValueConst*)&request;
        auto val = JS_Call(ctx, export_func, JS_UNDEFINED, argc, argv);
        JS_FreeValue(ctx, request);
        JS_FreeValue(ctx, export_func);

        if (JS_IsException(val))
        {
          js_dump_error(ctx);
          args.rpc_ctx->set_response_status(HTTP_STATUS_INTERNAL_SERVER_ERROR);
          args.rpc_ctx->set_response_body("Exception thrown while executing");
          return;
        }

        // Handle return value: {body, headers, statusCode}
        if (!JS_IsObject(val))
        {
          args.rpc_ctx->set_response_status(HTTP_STATUS_INTERNAL_SERVER_ERROR);
          args.rpc_ctx->set_response_body(
            "Invalid endpoint function return value");
          return;
        }

        // Response body (also sets a default response content-type header)
        auto response_body_js = JS_GetPropertyStr(ctx, val, "body");
        std::vector<uint8_t> response_body;
        size_t buf_size;
        size_t buf_offset;
        JSValue typed_array_buffer = JS_GetTypedArrayBuffer(
          ctx, response_body_js, &buf_offset, &buf_size, nullptr);
        uint8_t* array_buffer;
        if (!JS_IsException(typed_array_buffer))
        {
          size_t buf_size_total;
          array_buffer =
            JS_GetArrayBuffer(ctx, &buf_size_total, typed_array_buffer);
          array_buffer += buf_offset;
          JS_FreeValue(ctx, typed_array_buffer);
        }
        else
        {
          array_buffer = JS_GetArrayBuffer(ctx, &buf_size, response_body_js);
        }
        if (array_buffer)
        {
          args.rpc_ctx->set_response_header(
            http::headers::CONTENT_TYPE,
            http::headervalues::contenttype::OCTET_STREAM);
          response_body =
            std::vector<uint8_t>(array_buffer, array_buffer + buf_size);
        }
        else
        {
          const char* cstr = nullptr;
          if (JS_IsString(response_body_js))
          {
            args.rpc_ctx->set_response_header(
              http::headers::CONTENT_TYPE,
              http::headervalues::contenttype::TEXT);
            cstr = JS_ToCString(ctx, response_body_js);
          }
          else
          {
            args.rpc_ctx->set_response_header(
              http::headers::CONTENT_TYPE,
              http::headervalues::contenttype::JSON);
            JSValue rval =
              JS_JSONStringify(ctx, response_body_js, JS_NULL, JS_NULL);
            cstr = JS_ToCString(ctx, rval);
            JS_FreeValue(ctx, rval);
          }
          std::string str(cstr);
          JS_FreeCString(ctx, cstr);

          response_body = std::vector<uint8_t>(str.begin(), str.end());
        }
        JS_FreeValue(ctx, response_body_js);
        args.rpc_ctx->set_response_body(std::move(response_body));

        // Response headers
        auto response_headers_js = JS_GetPropertyStr(ctx, val, "headers");
        if (JS_IsObject(response_headers_js))
        {
          uint32_t prop_count = 0;
          JSPropertyEnum* props = nullptr;
          JS_GetOwnPropertyNames(
            ctx,
            &props,
            &prop_count,
            response_headers_js,
            JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY);
          for (size_t i = 0; i < prop_count; i++)
          {
            auto prop_name = props[i].atom;
            auto prop_name_cstr = JS_AtomToCString(ctx, prop_name);
            auto prop_val = JS_GetProperty(ctx, response_headers_js, prop_name);
            auto prop_val_cstr = JS_ToCString(ctx, prop_val);
            if (!prop_val_cstr)
            {
              args.rpc_ctx->set_response_status(
                HTTP_STATUS_INTERNAL_SERVER_ERROR);
              args.rpc_ctx->set_response_body("Invalid header value type");
              return;
            }
            args.rpc_ctx->set_response_header(prop_name_cstr, prop_val_cstr);
            JS_FreeCString(ctx, prop_name_cstr);
            JS_FreeCString(ctx, prop_val_cstr);
            JS_FreeValue(ctx, prop_val);
          }
          js_free(ctx, props);
        }
        JS_FreeValue(ctx, response_headers_js);

        // Response status code
        int response_status_code = HTTP_STATUS_OK;
        auto status_code_js = JS_GetPropertyStr(ctx, val, "statusCode");
        if (JS_VALUE_GET_TAG(status_code_js) == JS_TAG_INT)
        {
          response_status_code = JS_VALUE_GET_INT(status_code_js);
        }
        JS_FreeValue(ctx, status_code_js);
        args.rpc_ctx->set_response_status(response_status_code);

        JS_FreeValue(ctx, val);

        JS_FreeContext(ctx);
        JS_FreeRuntime(rt);

        return;
      };

      set_default(default_handler);
    }

    static std::pair<http_method, std::string> split_script_key(
      const std::string& key)
    {
      size_t s = key.find(' ');
      if (s != std::string::npos)
      {
        return std::make_pair(
          http::http_method_from_str(key.substr(0, s).c_str()),
          key.substr(s + 1, key.size() - (s + 1)));
      }
      else
      {
        return std::make_pair(HTTP_POST, key);
      }
    }

    // Since we do our own dispatch within the default handler, report the
    // supported methods here
    void build_api(nlohmann::json& document, kv::Tx& tx) override
    {
      UserEndpointRegistry::build_api(document, tx);

      auto scripts = tx.get_view(this->network.app_scripts);
      scripts->foreach([&document](const auto& key, const auto&) {
        const auto [verb, method] = split_script_key(key);

        ds::openapi::path_operation(ds::openapi::path(document, method), verb);
        return true;
      });
    }
  };

#pragma clang diagnostic pop

  class JS : public ccf::UserRpcFrontend
  {
  private:
    JSHandlers js_handlers;

  public:
    JS(NetworkTables& network) :
      ccf::UserRpcFrontend(*network.tables, js_handlers),
      js_handlers(network)
    {}
  };

  std::shared_ptr<ccf::UserRpcFrontend> get_rpc_handler(
    NetworkTables& network, ccfapp::AbstractNodeContext&)
  {
    return make_shared<JS>(network);
  }
} // namespace ccfapp
