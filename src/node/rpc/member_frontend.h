// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#pragma once
#include "ccf/common_auth_policies.h"
#include "ccf/common_endpoint_registry.h"
#include "crypto/key_pair.h"
#include "ds/nonstd.h"
#include "frontend.h"
#include "js/wrap.h"
#include "lua_interp/lua_json.h"
#include "lua_interp/tx_script_runner.h"
#include "node/genesis_gen.h"
#include "node/gov.h"
#include "node/jwt.h"
#include "node/members.h"
#include "node/nodes.h"
#include "node/quote.h"
#include "node/rpc/json_handler.h"
#include "node/secret_share.h"
#include "node/share_manager.h"
#include "node_interface.h"
#include "tls/base64.h"

#include <charconv>
#include <exception>
#include <initializer_list>
#include <map>
#include <memory>
#include <set>
#include <sstream>

namespace ccf
{
  class MemberTsr : public lua::TxScriptRunner
  {
    void setup_environment(
      lua::Interpreter& li,
      const std::optional<Script>& env_script) const override
    {
      TxScriptRunner::setup_environment(li, env_script);
    }

  public:
    MemberTsr(NetworkTables& network) : TxScriptRunner(network) {}
  };

  struct SetMemberData
  {
    MemberId member_id;
    nlohmann::json member_data = nullptr;
  };
  DECLARE_JSON_TYPE_WITH_OPTIONAL_FIELDS(SetMemberData)
  DECLARE_JSON_REQUIRED_FIELDS(SetMemberData, member_id)
  DECLARE_JSON_OPTIONAL_FIELDS(SetMemberData, member_data)

  struct SetUserData
  {
    UserId user_id;
    nlohmann::json user_data = nullptr;
  };
  DECLARE_JSON_TYPE_WITH_OPTIONAL_FIELDS(SetUserData)
  DECLARE_JSON_REQUIRED_FIELDS(SetUserData, user_id)
  DECLARE_JSON_OPTIONAL_FIELDS(SetUserData, user_data)

  struct SetModule
  {
    std::string name;
    Module module;
  };
  DECLARE_JSON_TYPE(SetModule)
  DECLARE_JSON_REQUIRED_FIELDS(SetModule, name, module)

  struct JsBundleEndpointMethod : public ccf::endpoints::EndpointProperties
  {
    std::string js_module;
    std::string js_function;
  };
  DECLARE_JSON_TYPE_WITH_BASE(
    JsBundleEndpointMethod, ccf::endpoints::EndpointProperties)
  DECLARE_JSON_REQUIRED_FIELDS(JsBundleEndpointMethod, js_module, js_function)

  using JsBundleEndpoint = std::map<std::string, JsBundleEndpointMethod>;

  struct JsBundleMetadata
  {
    std::map<std::string, JsBundleEndpoint> endpoints;
  };
  DECLARE_JSON_TYPE(JsBundleMetadata)
  DECLARE_JSON_REQUIRED_FIELDS(JsBundleMetadata, endpoints)

  struct JsBundle
  {
    JsBundleMetadata metadata;
    std::vector<SetModule> modules;
  };
  DECLARE_JSON_TYPE(JsBundle)
  DECLARE_JSON_REQUIRED_FIELDS(JsBundle, metadata, modules)

  struct DeployJsApp
  {
    JsBundle bundle;
  };
  DECLARE_JSON_TYPE(DeployJsApp)
  DECLARE_JSON_REQUIRED_FIELDS(DeployJsApp, bundle)

  struct SetJwtIssuer : public ccf::JwtIssuerMetadata
  {
    std::string issuer;
    std::optional<JsonWebKeySet> jwks;
  };
  DECLARE_JSON_TYPE_WITH_BASE_AND_OPTIONAL_FIELDS(
    SetJwtIssuer, ccf::JwtIssuerMetadata)
  DECLARE_JSON_REQUIRED_FIELDS(SetJwtIssuer, issuer)
  DECLARE_JSON_OPTIONAL_FIELDS(SetJwtIssuer, jwks)

  struct RemoveJwtIssuer
  {
    std::string issuer;
  };
  DECLARE_JSON_TYPE(RemoveJwtIssuer)
  DECLARE_JSON_REQUIRED_FIELDS(RemoveJwtIssuer, issuer)

  struct SetJwtPublicSigningKeys
  {
    std::string issuer;
    JsonWebKeySet jwks;
  };
  DECLARE_JSON_TYPE(SetJwtPublicSigningKeys)
  DECLARE_JSON_REQUIRED_FIELDS(SetJwtPublicSigningKeys, issuer, jwks)

  struct SetCaCertBundle
  {
    std::string name;
    std::string cert_bundle;
  };
  DECLARE_JSON_TYPE(SetCaCertBundle)
  DECLARE_JSON_REQUIRED_FIELDS(SetCaCertBundle, name, cert_bundle)

  class MemberEndpoints : public CommonEndpointRegistry
  {
  private:
    Script get_script(kv::Tx& tx, std::string name)
    {
      const auto s = tx.ro(network.gov_scripts)->get(name);
      if (!s)
      {
        throw std::logic_error(
          fmt::format("Could not find gov script: {}", name));
      }
      return *s;
    }

    void set_js_scripts(kv::Tx& tx, std::map<std::string, std::string> scripts)
    {
      auto tx_scripts = tx.rw(network.app_scripts);

      // First, remove all existing handlers
      tx_scripts->foreach(
        [&tx_scripts](const std::string& name, const Script&) {
          tx_scripts->remove(name);
          return true;
        });

      for (auto& rs : scripts)
      {
        tx_scripts->put(rs.first, {rs.second});
      }
    }

    bool deploy_js_app(kv::Tx& tx, const JsBundle& bundle)
    {
      std::string module_prefix = "/";
      remove_modules(tx, module_prefix);
      set_modules(tx, module_prefix, bundle.modules);

      remove_endpoints(tx);

      auto endpoints =
        tx.rw<ccf::endpoints::EndpointsMap>(ccf::Tables::ENDPOINTS);

      std::map<std::string, std::string> scripts;
      for (auto& [url, endpoint] : bundle.metadata.endpoints)
      {
        for (auto& [method, info] : endpoint)
        {
          const std::string& js_module = info.js_module;
          if (std::none_of(
                bundle.modules.cbegin(),
                bundle.modules.cend(),
                [&js_module](const SetModule& item) {
                  return item.name == js_module;
                }))
          {
            LOG_FAIL_FMT(
              "{} {}: module '{}' not found in bundle",
              method,
              url,
              info.js_module);
            return false;
          }

          auto verb = nlohmann::json(method).get<RESTVerb>();
          endpoints->put(ccf::endpoints::EndpointKey{url, verb}, info);

          // CCF currently requires each endpoint to have an inline JS module.
          std::string method_uppercase = method;
          nonstd::to_upper(method_uppercase);
          std::string url_without_leading_slash = url.substr(1);
          std::string key =
            fmt::format("{} {}", method_uppercase, url_without_leading_slash);
          std::string script = fmt::format(
            "import {{ {} as f }} from '.{}{}'; export default (r) => f(r);",
            info.js_function,
            module_prefix,
            info.js_module);
          scripts.emplace(key, script);
        }
      }

      set_js_scripts(tx, scripts);

      return true;
    }

    bool remove_js_app(kv::Tx& tx)
    {
      remove_modules(tx, "/");
      set_js_scripts(tx, {});

      return true;
    }

    void set_modules(
      kv::Tx& tx, std::string prefix, const std::vector<SetModule>& modules)
    {
      for (auto& set_module_ : modules)
      {
        std::string full_name = prefix + set_module_.name;
        if (!set_module(tx, full_name, set_module_.module))
        {
          throw std::logic_error(
            fmt::format("Unexpected error while setting module {}", full_name));
        }
      }
    }

    bool set_module(kv::Tx& tx, std::string name, Module module)
    {
      if (name.empty() || name[0] != '/')
      {
        LOG_FAIL_FMT("module names must start with /");
        return false;
      }
      auto tx_modules = tx.rw(network.modules);
      tx_modules->put(name, module);
      return true;
    }

    void remove_modules(kv::Tx& tx, std::string prefix)
    {
      auto tx_modules = tx.rw(network.modules);
      tx_modules->foreach(
        [&tx_modules, &prefix](const std::string& name, const Module&) {
          if (nonstd::starts_with(name, prefix))
          {
            if (!tx_modules->remove(name))
            {
              throw std::logic_error(
                fmt::format("Unexpected error while removing module {}", name));
            }
          }
          return true;
        });
    }

    bool remove_module(kv::Tx& tx, std::string name)
    {
      auto tx_modules = tx.rw(network.modules);
      return tx_modules->remove(name);
    }

    void remove_endpoints(kv::Tx& tx)
    {
      auto endpoints =
        tx.rw<ccf::endpoints::EndpointsMap>(ccf::Tables::ENDPOINTS);
      endpoints->foreach([&endpoints](const auto& k, const auto&) {
        endpoints->remove(k);
        return true;
      });
    }

    bool add_new_code_id(
      kv::Tx& tx,
      const CodeDigest& new_code_id,
      CodeIDs& code_id_table,
      const ProposalId& proposal_id)
    {
      auto code_ids = tx.rw(code_id_table);
      auto existing_code_id = code_ids->get(new_code_id);
      if (existing_code_id)
      {
        LOG_FAIL_FMT(
          "Proposal {}: Code signature already exists with digest: {}",
          proposal_id,
          ds::to_hex(new_code_id.data));
        return false;
      }
      code_ids->put(new_code_id, CodeStatus::ALLOWED_TO_JOIN);
      return true;
    }

    bool retire_code_id(
      kv::Tx& tx,
      const CodeDigest& code_id,
      CodeIDs& code_id_table,
      const ProposalId& proposal_id)
    {
      auto code_ids = tx.rw(code_id_table);
      auto existing_code_id = code_ids->get(code_id);
      if (!existing_code_id)
      {
        LOG_FAIL_FMT(
          "Proposal {}: No such code id in table: {}",
          proposal_id,
          ds::to_hex(code_id.data));
        return false;
      }
      code_ids->remove(code_id);
      return true;
    }

    //! Table of functions that proposal scripts can propose to invoke
    const std::unordered_map<
      std::string,
      std::function<bool(const ProposalId&, kv::Tx&, const nlohmann::json&)>>
      hardcoded_funcs = {
        // set the js application script
        {"set_js_app",
         [this](const ProposalId&, kv::Tx& tx, const nlohmann::json& args) {
           const std::string app = args;
           set_js_scripts(tx, lua::Interpreter().invoke<nlohmann::json>(app));
           return true;
         }},
        // deploy the js application bundle
        {"deploy_js_app",
         [this](const ProposalId&, kv::Tx& tx, const nlohmann::json& args) {
           const auto parsed = args.get<DeployJsApp>();
           return deploy_js_app(tx, parsed.bundle);
         }},
        // undeploy/remove the js application
        {"remove_js_app",
         [this](const ProposalId&, kv::Tx& tx, const nlohmann::json&) {
           return remove_js_app(tx);
         }},
        // add/update a module
        {"set_module",
         [this](const ProposalId&, kv::Tx& tx, const nlohmann::json& args) {
           const auto parsed = args.get<SetModule>();
           return set_module(tx, parsed.name, parsed.module);
         }},
        // remove a module
        {"remove_module",
         [this](const ProposalId&, kv::Tx& tx, const nlohmann::json& args) {
           const auto name = args.get<std::string>();
           return remove_module(tx, name);
         }},
        // add a new member
        {"new_member",
         [this](const ProposalId&, kv::Tx& tx, const nlohmann::json& args) {
           const auto parsed = args.get<NewMember>();
           GenesisGenerator g(this->network, tx);
           g.add_member(parsed);

           return true;
         }},
        // retire an existing member
        {"remove_member",
         [this](const ProposalId&, kv::Tx& tx, const nlohmann::json& args) {
           const auto member_id = args.get<MemberId>();

           GenesisGenerator g(this->network, tx);
           bool is_active = g.is_active_member(member_id);
           bool is_recovery = g.is_recovery_member(member_id);
           if (!g.remove_member(member_id))
           {
             return false;
           }

           if (is_active && is_recovery)
           {
             // A retired recovery member should not have access to the private
             // ledger going forward so rekey ledger, issuing new share to
             // remaining active members
             if (!context.get_node_state().rekey_ledger(tx))
             {
               return false;
             }
           }

           return true;
         }},
        {"set_member_data",
         [this](
           const ProposalId& proposal_id,
           kv::Tx& tx,
           const nlohmann::json& args) {
           const auto parsed = args.get<SetMemberData>();
           auto members = tx.rw(this->network.member_info);
           auto member_info = members->get(parsed.member_id);
           if (!member_info.has_value())
           {
             LOG_FAIL_FMT(
               "Proposal {}: {} is not a valid member ID",
               proposal_id,
               parsed.member_id);
             return false;
           }

           member_info->member_data = parsed.member_data;
           members->put(parsed.member_id, member_info.value());
           return true;
         }},
        {"new_user",
         [this](const ProposalId&, kv::Tx& tx, const nlohmann::json& args) {
           const auto user_info = args.get<NewUser>();

           GenesisGenerator g(this->network, tx);
           g.add_user(user_info);

           return true;
         }},
        {"remove_user",
         [this](const ProposalId&, kv::Tx& tx, const nlohmann::json& args) {
           const UserId user_id = args;

           GenesisGenerator g(this->network, tx);
           g.remove_user(user_id);

           return true;
         }},
        {"set_user_data",
         [this](
           const ProposalId& proposal_id,
           kv::Tx& tx,
           const nlohmann::json& args) {
           const auto parsed = args.get<SetUserData>();
           auto users = tx.rw(this->network.user_certs);
           auto user = users->get(parsed.user_id);
           if (!user.has_value())
           {
             LOG_FAIL_FMT(
               "Proposal {}: {} is not a valid user",
               proposal_id,
               parsed.user_id);
             return false;
           }

           auto user_info = tx.rw(this->network.user_info);
           user_info->put(parsed.user_id, {parsed.user_data});
           return true;
         }},
        {"set_ca_cert_bundle",
         [this](
           const ProposalId& proposal_id,
           kv::Tx& tx,
           const nlohmann::json& args) {
           const auto parsed = args.get<SetCaCertBundle>();
           auto ca_cert_bundles = tx.rw(this->network.ca_cert_bundles);
           try
           {
             tls::CA(parsed.cert_bundle);
           }
           catch (const std::logic_error& e)
           {
             LOG_FAIL_FMT(
               "Proposal {}: 'cert_bundle' is not a valid X.509 certificate "
               "bundle in "
               "PEM format: {}",
               proposal_id,
               e.what());
             return false;
           }
           ca_cert_bundles->put(parsed.name, parsed.cert_bundle);
           return true;
         }},
        {"remove_ca_cert_bundle",
         [this](const ProposalId&, kv::Tx& tx, const nlohmann::json& args) {
           const auto cert_bundle_name = args.get<std::string>();
           auto ca_cert_bundles = tx.rw(this->network.ca_cert_bundles);
           ca_cert_bundles->remove(cert_bundle_name);
           return true;
         }},
        {"set_jwt_issuer",
         [this](
           const ProposalId& proposal_id,
           kv::Tx& tx,
           const nlohmann::json& args) {
           const auto parsed = args.get<SetJwtIssuer>();
           auto issuers = tx.rw(this->network.jwt_issuers);
           auto ca_cert_bundles = tx.ro(this->network.ca_cert_bundles);

           if (parsed.auto_refresh)
           {
             if (!parsed.ca_cert_bundle_name.has_value())
             {
               LOG_FAIL_FMT(
                 "Proposal {}: ca_cert_bundle_name is missing but required if "
                 "auto_refresh is true",
                 proposal_id);
               return false;
             }
             if (!ca_cert_bundles->has(parsed.ca_cert_bundle_name.value()))
             {
               LOG_FAIL_FMT(
                 "Proposal {}: No CA cert list found with name '{}'",
                 proposal_id,
                 parsed.ca_cert_bundle_name.value());
               return false;
             }
             http::URL issuer_url;
             try
             {
               issuer_url = http::parse_url_full(parsed.issuer);
             }
             catch (const std::runtime_error&)
             {
               LOG_FAIL_FMT(
                 "Proposal {}: issuer must be a URL if auto_refresh is true",
                 proposal_id);
               return false;
             }
             if (issuer_url.scheme != "https")
             {
               LOG_FAIL_FMT(
                 "Proposal {}: issuer must be a URL starting with https:// if "
                 "auto_refresh is true",
                 proposal_id);
               return false;
             }
             if (!issuer_url.query.empty() || !issuer_url.fragment.empty())
             {
               LOG_FAIL_FMT(
                 "Proposal {}: issuer must be a URL without query/fragment if "
                 "auto_refresh is true",
                 proposal_id);
               return false;
             }
           }

           bool success = true;
           if (parsed.jwks.has_value())
           {
             success = set_jwt_public_signing_keys(
               tx, proposal_id, parsed.issuer, parsed, parsed.jwks.value());
           }
           if (success)
           {
             issuers->put(parsed.issuer, parsed);
           }

           return success;
         }},
        {"remove_jwt_issuer",
         [this](
           const ProposalId& proposal_id,
           kv::Tx& tx,
           const nlohmann::json& args) {
           const auto parsed = args.get<RemoveJwtIssuer>();
           const auto issuer = parsed.issuer;
           auto issuers = tx.rw(this->network.jwt_issuers);

           if (!issuers->remove(issuer))
           {
             LOG_FAIL_FMT(
               "Proposal {}: {} is not a valid issuer", proposal_id, issuer);
             return false;
           }

           remove_jwt_keys(tx, issuer);

           return true;
         }},
        {"set_jwt_public_signing_keys",
         [this](
           const ProposalId& proposal_id,
           kv::Tx& tx,
           const nlohmann::json& args) {
           const auto parsed = args.get<SetJwtPublicSigningKeys>();

           auto issuers = tx.rw(this->network.jwt_issuers);
           auto issuer_metadata_ = issuers->get(parsed.issuer);
           if (!issuer_metadata_.has_value())
           {
             LOG_FAIL_FMT(
               "Proposal {}: {} is not a valid issuer",
               proposal_id,
               parsed.issuer);
             return false;
           }
           auto& issuer_metadata = issuer_metadata_.value();

           return set_jwt_public_signing_keys(
             tx, proposal_id, parsed.issuer, issuer_metadata, parsed.jwks);
         }},
        // accept a node
        {"trust_node",
         [this](
           const ProposalId& proposal_id,
           kv::Tx& tx,
           const nlohmann::json& args) {
           const auto node_id = args.get<NodeId>();
           try
           {
             GenesisGenerator g(network, tx);
             g.trust_node(
               node_id, network.ledger_secrets->get_latest(tx).first);
           }
           catch (const std::logic_error& e)
           {
             LOG_FAIL_FMT("Proposal {} failed: {}", proposal_id, e.what());
             return false;
           }
           return true;
         }},
        // retire a node
        {"retire_node",
         [this](
           const ProposalId& proposal_id,
           kv::Tx& tx,
           const nlohmann::json& args) {
           const auto id = args.get<NodeId>();
           auto nodes = tx.rw(this->network.nodes);
           auto node_info = nodes->get(id);
           if (!node_info.has_value())
           {
             LOG_FAIL_FMT(
               "Proposal {}: Node {} does not exist", proposal_id, id);
             return false;
           }
           if (node_info->status == NodeStatus::RETIRED)
           {
             LOG_FAIL_FMT(
               "Proposal {}: Node {} is already retired", proposal_id, id);
             return false;
           }
           node_info->status = NodeStatus::RETIRED;
           nodes->put(id, node_info.value());
           LOG_INFO_FMT("Node {} is now {}", id, node_info->status);
           return true;
         }},
        // accept new node code ID
        {"new_node_code",
         [this](
           const ProposalId& proposal_id,
           kv::Tx& tx,
           const nlohmann::json& args) {
           return this->add_new_code_id(
             tx,
             args.get<CodeDigest>(),
             this->network.node_code_ids,
             proposal_id);
         }},
        // retire node code ID
        {"retire_node_code",
         [this](
           const ProposalId& proposal_id,
           kv::Tx& tx,
           const nlohmann::json& args) {
           return this->retire_code_id(
             tx,
             args.get<CodeDigest>(),
             this->network.node_code_ids,
             proposal_id);
         }},
        {"transition_service_to_open",
         [this](
           const ProposalId& proposal_id, kv::Tx& tx, const nlohmann::json&) {
           auto service = tx.ro<Service>(Tables::SERVICE)->get(0);
           if (!service.has_value())
           {
             throw std::logic_error(
               "Service information cannot be found in current state");
           }

           // Idempotence: if the service is already open or waiting for
           // recovery shares, the proposal should succeed
           if (
             service->status == ServiceStatus::WAITING_FOR_RECOVERY_SHARES ||
             service->status == ServiceStatus::OPEN)
           {
             return true;
           }

           if (context.get_node_state().is_part_of_public_network())
           {
             // If the node is in public mode, start accepting member recovery
             // shares
             const auto accept_recovery =
               context.get_node_state().accept_recovery(tx);
             if (!accept_recovery)
             {
               LOG_FAIL_FMT(
                 "Proposal {}: Failed to accept recovery", proposal_id);
             }
             return accept_recovery;
           }
           else if (context.get_node_state().is_part_of_network())
           {
             // Otherwise, if the node is part of the network. Open the network
             // straight away. We first check that a sufficient number of
             // recovery members have become active. If so, recovery shares are
             // allocated to each recovery member.
             try
             {
               share_manager.issue_recovery_shares(tx);
             }
             catch (const std::logic_error& e)
             {
               LOG_FAIL_FMT(
                 "Proposal {}: Failed to issuing recovery shares failed when "
                 "transitioning the service to open network: {}",
                 proposal_id,
                 e.what());
               return false;
             }

             GenesisGenerator g(this->network, tx);
             const auto network_opened = g.open_service();
             if (!network_opened)
             {
               LOG_FAIL_FMT("Proposal {}: Failed to open service", proposal_id);
             }
             else
             {
               context.get_node_state().open_user_frontend();
             }
             return network_opened;
           }

           LOG_FAIL_FMT(
             "Proposal {}: Service is not in expected state to transition to "
             "open",
             proposal_id);
           return false;
         }},
        {"rekey_ledger",
         [this](
           const ProposalId& proposal_id, kv::Tx& tx, const nlohmann::json&) {
           const auto ledger_rekeyed =
             context.get_node_state().rekey_ledger(tx);
           if (!ledger_rekeyed)
           {
             LOG_FAIL_FMT("Proposal {}: Ledger rekey failed", proposal_id);
           }
           return ledger_rekeyed;
         }},
        {"update_recovery_shares",
         [this](
           const ProposalId& proposal_id, kv::Tx& tx, const nlohmann::json&) {
           try
           {
             share_manager.shuffle_recovery_shares(tx);
           }
           catch (const std::logic_error& e)
           {
             LOG_FAIL_FMT(
               "Proposal {}: Updating recovery shares failed: {}",
               proposal_id,
               e.what());
             return false;
           }
           return true;
         }},
        {"set_recovery_threshold",
         [this](
           const ProposalId& proposal_id,
           kv::Tx& tx,
           const nlohmann::json& args) {
           const auto new_recovery_threshold = args.get<size_t>();

           GenesisGenerator g(this->network, tx);

           if (new_recovery_threshold == g.get_recovery_threshold())
           {
             // If the recovery threshold is the same as before, return with no
             // effect
             return true;
           }

           if (!g.set_recovery_threshold(new_recovery_threshold))
           {
             return false;
           }

           try
           {
             share_manager.shuffle_recovery_shares(tx);
           }
           catch (const std::logic_error& e)
           {
             LOG_FAIL_FMT(
               "Proposal {}: Setting recovery threshold failed: {}",
               proposal_id,
               e.what());
             return false;
           }
           return true;
         }},
      };

    ProposalInfo complete_proposal(
      kv::Tx& tx, const ProposalId& proposal_id, Proposal& proposal)
    {
      if (proposal.state != ProposalState::OPEN)
      {
        throw std::logic_error(fmt::format(
          "Cannot complete non-open proposal - current state is {}",
          proposal.state));
      }

      auto proposals = tx.rw(this->network.proposals);

      // run proposal script
      const auto proposed_calls = tsr.run<nlohmann::json>(
        tx,
        {proposal.script,
         {}, // can't write
         WlIds::MEMBER_CAN_READ,
         get_script(tx, GovScriptIds::ENV_PROPOSAL)},
        // vvv arguments to script vvv
        proposal.parameter);

      nlohmann::json votes = nlohmann::json::object();
      // Collect all member votes
      for (const auto& vote : proposal.votes)
      {
        // valid voter
        if (!check_member_active(tx, vote.first))
        {
          continue;
        }

        // does the voter agree?
        votes[vote.first.value()] = tsr.run<bool>(
          tx,
          {vote.second,
           {}, // can't write
           WlIds::MEMBER_CAN_READ,
           {}},
          proposed_calls);
      }

      const auto pass = tsr.run<int>(
        tx,
        {get_script(tx, GovScriptIds::PASS),
         {}, // can't write
         WlIds::MEMBER_CAN_READ,
         {}},
        // vvv arguments to script vvv
        proposed_calls,
        votes,
        proposal.proposer);

      switch (pass)
      {
        case CompletionResult::PASSED:
        {
          // vote passed, go on to update the state
          break;
        }
        case CompletionResult::PENDING:
        {
          // vote is pending, return false but do not update state
          return get_proposal_info(proposal_id, proposal);
        }
        case CompletionResult::REJECTED:
        {
          // vote unsuccessful, update the proposal's state
          proposal.state = ProposalState::REJECTED;
          proposals->put(proposal_id, proposal);
          return get_proposal_info(proposal_id, proposal);
        }
        default:
        {
          throw std::logic_error(fmt::format(
            "Invalid completion result ({}) for proposal {}",
            pass,
            proposal_id));
        }
      };

      // execute proposed calls
      ProposedCalls pc = proposed_calls;
      std::optional<std::string> unknown_call = std::nullopt;
      for (const auto& call : pc)
      {
        // proposing a hardcoded C++ function?
        const auto f = hardcoded_funcs.find(call.func);
        if (f != hardcoded_funcs.end())
        {
          if (!f->second(proposal_id, tx, call.args))
          {
            proposal.state = ProposalState::FAILED;
            proposals->put(proposal_id, proposal);
            return get_proposal_info(proposal_id, proposal);
          }
          continue;
        }

        // proposing a script function?
        const auto s = tx.rw(network.gov_scripts)->get(call.func);
        if (!s.has_value())
        {
          unknown_call = call.func;
          break;
        }
        tsr.run<void>(
          tx,
          {s.value(),
           WlIds::MEMBER_CAN_PROPOSE, // can write!
           {},
           {}},
          call.args);
      }

      if (!unknown_call.has_value())
      {
        // if the vote was successful, update the proposal's state
        proposal.state = ProposalState::ACCEPTED;
      }
      else
      {
        // If any function in the proposal is unknown, mark the proposal as
        // failed
        LOG_FAIL_FMT(
          "Proposal {}: \"{}\" call is unknown",
          proposal_id,
          unknown_call.value());
        proposal.state = ProposalState::FAILED;
      }
      proposals->put(proposal_id, proposal);

      return get_proposal_info(proposal_id, proposal);
    }

#ifdef ENABLE_JS_GOV

#  pragma clang diagnostic push
#  pragma clang diagnostic ignored "-Wc99-extensions"

    ccf::jsgov::ProposalInfoSummary resolve_proposal(
      kv::Tx& tx,
      const ProposalId& proposal_id,
      const std::vector<uint8_t>& proposal,
      const std::string& constitution)
    {
      auto pi =
        tx.rw<ccf::jsgov::ProposalInfoMap>("public:ccf.gov.proposals_info.js");
      auto pi_ = pi->get(proposal_id);

      std::vector<std::pair<MemberId, bool>> votes;
      for (const auto& [mid, mb] : pi_->ballots)
      {
        std::string mbs = fmt::format(
          "{}\n export default (proposal, proposer_id) => vote(proposal, "
          "proposer_id);",
          mb);

        js::Runtime rt;
        js::Context context(rt);
        rt.add_ccf_classdefs();
        js::TxContext txctx{&tx, js::TxAccess::GOV_RO};
        js::populate_global_ccf(&txctx, std::nullopt, nullptr, context);
        auto ballot_func = context.function(
          mbs, fmt::format("ballot from {} for {}", mid, proposal_id));

        JSValue argv[2];
        auto prop = JS_NewStringLen(
          context, (const char*)proposal.data(), proposal.size());
        argv[0] = prop;
        auto pid = JS_NewStringLen(
          context, pi_->proposer_id.data(), pi_->proposer_id.size());
        argv[1] = pid;

        auto val =
          context(JS_Call(context, ballot_func, JS_UNDEFINED, 2, argv));
        if (!JS_IsException(val))
        {
          votes.emplace_back(mid, JS_ToBool(context, val));
        }
        JS_FreeValue(context, ballot_func);
        JS_FreeValue(context, prop);
        JS_FreeValue(context, pid);
      }

      {
        std::string mbs = fmt::format(
          "{}\n export default (proposal, proposer_id, votes) => "
          "resolve(proposal, proposer_id, votes);",
          constitution);

        js::Runtime rt;
        js::Context context(rt);
        js::populate_global_console(context);
        rt.add_ccf_classdefs();
        js::TxContext txctx{&tx, js::TxAccess::GOV_RO};
        js::populate_global_ccf(&txctx, std::nullopt, nullptr, context);
        auto resolve_func =
          context.function(mbs, fmt::format("resolve {}", proposal_id));
        JSValue argv[3];
        auto prop = JS_NewStringLen(
          context, (const char*)proposal.data(), proposal.size());
        argv[0] = prop;

        auto prop_id = JS_NewStringLen(
          context, pi_->proposer_id.data(), pi_->proposer_id.size());
        argv[1] = prop_id;

        auto vs = JS_NewArray(context);
        size_t index = 0;
        for (auto& [mid, vote] : votes)
        {
          auto v = JS_NewObject(context);
          auto member_id = JS_NewStringLen(context, mid.data(), mid.size());
          JS_DefinePropertyValueStr(
            context, v, "member_id", member_id, JS_PROP_C_W_E);
          auto vote_status = JS_NewBool(context, vote);
          JS_DefinePropertyValueStr(
            context, v, "vote", vote_status, JS_PROP_C_W_E);
          JS_DefinePropertyValueUint32(context, vs, index++, v, JS_PROP_C_W_E);
        }
        argv[2] = vs;

        auto val =
          context(JS_Call(context, resolve_func, JS_UNDEFINED, 3, argv));

        JS_FreeValue(context, resolve_func);
        JS_FreeValue(context, prop);
        JS_FreeValue(context, prop_id);
        JS_FreeValue(context, vs);

        if (JS_IsString(val))
        {
          auto s = JS_ToCString(context, val);
          std::string status(s);
          JS_FreeCString(context, s);
          if (status == "Open")
          {
            pi_.value().state = ProposalState::OPEN;
          }
          else if (status == "Accepted")
          {
            pi_.value().state = ProposalState::ACCEPTED;
          }
          else if (status == "Withdrawn")
          {
            pi_.value().state = ProposalState::FAILED;
          }
          else if (status == "Rejected")
          {
            pi_.value().state = ProposalState::REJECTED;
          }
          else if (status == "Failed")
          {
            pi_.value().state = ProposalState::FAILED;
          }
          else
          {
            pi_.value().state = ProposalState::FAILED;
          }
        }

        if (pi_.value().state != ProposalState::OPEN)
        {
          // Record votes and errors
          if (pi_.value().state == ProposalState::ACCEPTED)
          {
            std::string apply_script = fmt::format(
              "{}\n export default (proposal) => apply(proposal);",
              constitution);

            js::Runtime rt;
            js::Context context(rt);
            rt.add_ccf_classdefs();
            js::TxContext txctx{&tx, js::TxAccess::GOV_RW};
            js::populate_global_ccf(&txctx, std::nullopt, nullptr, context);
            auto apply_func = context.function(
              apply_script, fmt::format("apply for {}", proposal_id));

            auto prop = JS_NewStringLen(
              context, (const char*)proposal.data(), proposal.size());
            auto val =
              context(JS_Call(context, apply_func, JS_UNDEFINED, 1, &prop));
            JS_FreeValue(context, apply_func);
            JS_FreeValue(context, prop);
            if (JS_IsException(val))
            {
              js::js_dump_error(context);
              pi_.value().state = ProposalState::FAILED;
            }
          }
        }

        return jsgov::ProposalInfoSummary{proposal_id,
                                          pi_->proposer_id,
                                          pi_.value().state,
                                          pi_.value().ballots.size()};
      }
    }

#  pragma clang diagnostic pop
#endif

    bool check_member_active(kv::ReadOnlyTx& tx, const MemberId& id)
    {
      return check_member_status(tx, id, {MemberStatus::ACTIVE});
    }

    bool check_member_accepted(kv::ReadOnlyTx& tx, const MemberId& id)
    {
      return check_member_status(
        tx, id, {MemberStatus::ACTIVE, MemberStatus::ACCEPTED});
    }

    bool check_member_status(
      kv::ReadOnlyTx& tx,
      const MemberId& id,
      std::initializer_list<MemberStatus> allowed)
    {
      auto member = tx.ro(this->network.member_info)->get(id);
      if (!member.has_value())
      {
        return false;
      }
      for (const auto s : allowed)
      {
        if (member->status == s)
        {
          return true;
        }
      }
      return false;
    }

    void record_voting_history(
      kv::Tx& tx, const MemberId& caller_id, const SignedReq& signed_request)
    {
      auto governance_history = tx.rw(network.governance_history);
      governance_history->put(caller_id, {signed_request});
    }

    static ProposalInfo get_proposal_info(
      const ProposalId& proposal_id, const Proposal& proposal)
    {
      return ProposalInfo{proposal_id, proposal.proposer, proposal.state};
    }

    bool get_proposal_id_from_path(
      const enclave::PathParams& params,
      ProposalId& proposal_id,
      std::string& error)
    {
      return get_path_param(params, "proposal_id", proposal_id, error);
    }

    bool get_member_id_from_path(
      const enclave::PathParams& params,
      MemberId& member_id,
      std::string& error)
    {
      return get_path_param(params, "member_id", member_id.value(), error);
    }

    NetworkState& network;
    ShareManager& share_manager;
    const MemberTsr tsr;

  public:
    MemberEndpoints(
      NetworkState& network,
      ccfapp::AbstractNodeContext& context_,
      ShareManager& share_manager) :
      CommonEndpointRegistry(get_actor_prefix(ActorsType::members), context_),
      network(network),
      share_manager(share_manager),
      tsr(network)
    {
      openapi_info.title = "CCF Governance API";
      openapi_info.description =
        "This API is used to submit and query proposals which affect CCF's "
        "public governance tables.";
    }

    static std::optional<MemberId> get_caller_member_id(
      endpoints::CommandEndpointContext& ctx)
    {
      if (
        const auto* sig_ident =
          ctx.try_get_caller<ccf::MemberSignatureAuthnIdentity>())
      {
        return sig_ident->member_id;
      }
      else if (
        const auto* cert_ident =
          ctx.try_get_caller<ccf::MemberCertAuthnIdentity>())
      {
        return cert_ident->member_id;
      }

      LOG_FATAL_FMT("Request was not authenticated with a member auth policy");
      return std::nullopt;
    }

    void init_handlers() override
    {
      CommonEndpointRegistry::init_handlers();

      const AuthnPolicies member_sig_only = {member_signature_auth_policy};

      const AuthnPolicies member_cert_or_sig = {member_cert_auth_policy,
                                                member_signature_auth_policy};

      auto read = [this](auto& ctx, nlohmann::json&& params) {
        const auto member_id = get_caller_member_id(ctx);
        if (!member_id.has_value())
        {
          return make_error(
            HTTP_STATUS_FORBIDDEN,
            ccf::errors::AuthorizationFailed,
            "Member is unknown.");
        }

        if (!check_member_status(
              ctx.tx,
              member_id.value(),
              {MemberStatus::ACTIVE, MemberStatus::ACCEPTED}))
        {
          return make_error(
            HTTP_STATUS_FORBIDDEN,
            ccf::errors::AuthorizationFailed,
            "Member is not active or accepted.");
        }

        const auto in = params.get<KVRead::In>();

        const ccf::Script read_script(R"xxx(
        local tables, table_name, key = ...
        return tables[table_name]:get(key) or {}
        )xxx");

        auto value = tsr.run<nlohmann::json>(
          ctx.tx,
          {read_script, {}, WlIds::MEMBER_CAN_READ, {}},
          in.table,
          in.key);
        if (value.empty())
        {
          return make_error(
            HTTP_STATUS_NOT_FOUND,
            ccf::errors::KeyNotFound,
            fmt::format(
              "Key {} does not exist in table {}.", in.key.dump(), in.table));
        }

        return make_success(value);
      };
      make_endpoint("read", HTTP_POST, json_adapter(read), member_cert_or_sig)
        // This can be executed locally, but can't currently take ReadOnlyTx due
        // to restrictions in our lua wrappers
        .set_forwarding_required(endpoints::ForwardingRequired::Sometimes)
        .set_auto_schema<KVRead>()
        .install();

      auto query = [this](auto& ctx, nlohmann::json&& params) {
        const auto member_id = get_caller_member_id(ctx);
        if (!member_id.has_value())
        {
          return make_error(
            HTTP_STATUS_FORBIDDEN,
            ccf::errors::AuthorizationFailed,
            "Member is unknown.");
        }
        if (!check_member_accepted(ctx.tx, member_id.value()))
        {
          return make_error(
            HTTP_STATUS_FORBIDDEN,
            ccf::errors::AuthorizationFailed,
            "Member is not accepted.");
        }

        const auto script = params.get<ccf::Script>();
        return make_success(tsr.run<nlohmann::json>(
          ctx.tx, {script, {}, WlIds::MEMBER_CAN_READ, {}}));
      };
      make_endpoint("query", HTTP_POST, json_adapter(query), member_cert_or_sig)
        // This can be executed locally, but can't currently take ReadOnlyTx due
        // to restrictions in our lua wrappers
        .set_forwarding_required(endpoints::ForwardingRequired::Sometimes)
        .set_auto_schema<Script, nlohmann::json>()
        .install();

      auto propose = [this](auto& ctx, nlohmann::json&& params) {
        const auto& caller_identity =
          ctx.template get_caller<ccf::MemberSignatureAuthnIdentity>();
        if (!check_member_active(ctx.tx, caller_identity.member_id))
        {
          return make_error(
            HTTP_STATUS_FORBIDDEN,
            ccf::errors::AuthorizationFailed,
            "Member is not active.");
        }

        const auto in = params.get<Propose::In>();

        if (!consensus)
        {
          return make_error(
            HTTP_STATUS_INTERNAL_SERVER_ERROR,
            ccf::errors::InternalError,
            "No consensus available.");
        }

        std::string proposal_id;

        if (consensus->type() == ConsensusType::CFT)
        {
          auto root_at_read = ctx.tx.get_root_at_read_version();
          if (!root_at_read.has_value())
          {
            return make_error(
              HTTP_STATUS_INTERNAL_SERVER_ERROR,
              ccf::errors::InternalError,
              "Proposal failed to bind to state.");
          }

          // caller_identity.request_digest is set when getting the
          // MemberSignatureAuthnIdentity identity. The proposal id is a digest
          // of the root of the state tree at the read version and the request
          // digest.
          std::vector<uint8_t> acc(
            root_at_read.value().h.begin(), root_at_read.value().h.end());
          acc.insert(
            acc.end(),
            caller_identity.request_digest.begin(),
            caller_identity.request_digest.end());
          const crypto::Sha256Hash proposal_digest(acc);
          proposal_id = proposal_digest.hex_str();
        }
        else
        {
          proposal_id = fmt::format(
            "{:02x}", fmt::join(caller_identity.request_digest, ""));
        }

        Proposal proposal(in.script, in.parameter, caller_identity.member_id);
        auto proposals = ctx.tx.rw(this->network.proposals);
        // Introduce a read dependency, so that if identical proposal creations
        // are in-flight and reading at the same version, all except the first
        // conflict and are re-executed. If we ever produce a proposal ID which
        // already exists, we must have a hash collision.
        if (proposals->has(proposal_id))
        {
          return make_error(
            HTTP_STATUS_INTERNAL_SERVER_ERROR,
            ccf::errors::InternalError,
            "Proposal ID collision.");
        }
        proposals->put(proposal_id, proposal);

        record_voting_history(
          ctx.tx, caller_identity.member_id, caller_identity.signed_request);

        return make_success(
          Propose::Out{complete_proposal(ctx.tx, proposal_id, proposal)});
      };
      make_endpoint(
        "proposals", HTTP_POST, json_adapter(propose), member_sig_only)
        .set_auto_schema<Propose>()
        .install();

      auto get_proposal = [this](auto& ctx, nlohmann::json&&) {
        const auto member_id = get_caller_member_id(ctx);
        if (!member_id.has_value())
        {
          return make_error(
            HTTP_STATUS_FORBIDDEN,
            ccf::errors::AuthorizationFailed,
            "Member is unknown.");
        }

        if (!check_member_active(ctx.tx, member_id.value()))
        {
          return make_error(
            HTTP_STATUS_FORBIDDEN,
            ccf::errors::AuthorizationFailed,
            "Member is not active.");
        }

        ProposalId proposal_id;
        std::string error;
        if (!get_proposal_id_from_path(
              ctx.rpc_ctx->get_request_path_params(), proposal_id, error))
        {
          return make_error(
            HTTP_STATUS_BAD_REQUEST, ccf::errors::InvalidResourceName, error);
        }

        auto proposals = ctx.tx.ro(this->network.proposals);
        auto proposal = proposals->get(proposal_id);

        if (!proposal)
        {
          return make_error(
            HTTP_STATUS_BAD_REQUEST,
            ccf::errors::ProposalNotFound,
            fmt::format("Proposal {} does not exist.", proposal_id));
        }

        return make_success(proposal.value());
      };
      make_read_only_endpoint(
        "proposals/{proposal_id}",
        HTTP_GET,
        json_read_only_adapter(get_proposal),
        member_cert_or_sig)
        .set_auto_schema<void, Proposal>()
        .install();

      auto withdraw = [this](auto& ctx, nlohmann::json&&) {
        const auto& caller_identity =
          ctx.template get_caller<ccf::MemberSignatureAuthnIdentity>();
        if (!check_member_active(ctx.tx, caller_identity.member_id))
        {
          return make_error(
            HTTP_STATUS_FORBIDDEN,
            ccf::errors::AuthorizationFailed,
            "Member is not active.");
        }

        ProposalId proposal_id;
        std::string error;
        if (!get_proposal_id_from_path(
              ctx.rpc_ctx->get_request_path_params(), proposal_id, error))
        {
          return make_error(
            HTTP_STATUS_BAD_REQUEST, ccf::errors::InvalidResourceName, error);
        }

        auto proposals = ctx.tx.rw(this->network.proposals);
        auto proposal = proposals->get(proposal_id);

        if (!proposal)
        {
          return make_error(
            HTTP_STATUS_BAD_REQUEST,
            ccf::errors::ProposalNotFound,
            fmt::format("Proposal {} does not exist.", proposal_id));
        }

        if (proposal->proposer != caller_identity.member_id)
        {
          return make_error(
            HTTP_STATUS_FORBIDDEN,
            ccf::errors::AuthorizationFailed,
            fmt::format(
              "Proposal {} can only be withdrawn by proposer {}, not caller "
              "{}.",
              proposal_id,
              proposal->proposer,
              caller_identity.member_id));
        }

        if (proposal->state != ProposalState::OPEN)
        {
          return make_error(
            HTTP_STATUS_BAD_REQUEST,
            ccf::errors::ProposalNotOpen,
            fmt::format(
              "Proposal {} is currently in state {} - only {} proposals can be "
              "withdrawn.",
              proposal_id,
              proposal->state,
              ProposalState::OPEN));
        }

        proposal->state = ProposalState::WITHDRAWN;
        proposals->put(proposal_id, proposal.value());
        record_voting_history(
          ctx.tx, caller_identity.member_id, caller_identity.signed_request);

        return make_success(get_proposal_info(proposal_id, proposal.value()));
      };
      make_endpoint(
        "proposals/{proposal_id}/withdraw",
        HTTP_POST,
        json_adapter(withdraw),
        member_sig_only)
        .set_auto_schema<void, ProposalInfo>()
        .install();

      auto vote = [this](auto& ctx, nlohmann::json&& params) {
        const auto& caller_identity =
          ctx.template get_caller<ccf::MemberSignatureAuthnIdentity>();

        if (!check_member_active(ctx.tx, caller_identity.member_id))
        {
          return make_error(
            HTTP_STATUS_FORBIDDEN,
            ccf::errors::AuthorizationFailed,
            "Member is not active.");
        }

        ProposalId proposal_id;
        std::string error;
        if (!get_proposal_id_from_path(
              ctx.rpc_ctx->get_request_path_params(), proposal_id, error))
        {
          return make_error(
            HTTP_STATUS_BAD_REQUEST, ccf::errors::InvalidResourceName, error);
        }

        auto proposals = ctx.tx.rw(this->network.proposals);
        auto proposal = proposals->get(proposal_id);
        if (!proposal)
        {
          return make_error(
            HTTP_STATUS_NOT_FOUND,
            ccf::errors::ProposalNotFound,
            fmt::format("Proposal {} does not exist.", proposal_id));
        }

        if (proposal->state != ProposalState::OPEN)
        {
          return make_error(
            HTTP_STATUS_BAD_REQUEST,
            ccf::errors::ProposalNotOpen,
            fmt::format(
              "Proposal {} is currently in state {} - only {} proposals can "
              "receive votes.",
              proposal_id,
              proposal->state,
              ProposalState::OPEN));
        }

        const auto vote = params.get<Vote>();
        if (
          proposal->votes.find(caller_identity.member_id) !=
          proposal->votes.end())
        {
          return make_error(
            HTTP_STATUS_BAD_REQUEST,
            ccf::errors::VoteAlreadyExists,
            "Vote already submitted.");
        }
        proposal->votes[caller_identity.member_id] = vote.ballot;
        proposals->put(proposal_id, proposal.value());

        record_voting_history(
          ctx.tx, caller_identity.member_id, caller_identity.signed_request);

        return make_success(
          complete_proposal(ctx.tx, proposal_id, proposal.value()));
      };
      make_endpoint(
        "proposals/{proposal_id}/votes",
        HTTP_POST,
        json_adapter(vote),
        member_sig_only)
        .set_auto_schema<Vote, ProposalInfo>()
        .install();

      auto get_vote = [this](auto& ctx, nlohmann::json&&) {
        const auto member_id = get_caller_member_id(ctx);
        if (!member_id.has_value())
        {
          return make_error(
            HTTP_STATUS_FORBIDDEN,
            ccf::errors::AuthorizationFailed,
            "Member is unknown.");
        }

        if (!check_member_active(ctx.tx, member_id.value()))
        {
          return make_error(
            HTTP_STATUS_FORBIDDEN,
            ccf::errors::AuthorizationFailed,
            "Member is not active.");
        }

        std::string error;
        ProposalId proposal_id;
        if (!get_proposal_id_from_path(
              ctx.rpc_ctx->get_request_path_params(), proposal_id, error))
        {
          return make_error(
            HTTP_STATUS_BAD_REQUEST, ccf::errors::InvalidResourceName, error);
        }

        MemberId vote_member_id;
        if (!get_member_id_from_path(
              ctx.rpc_ctx->get_request_path_params(), vote_member_id, error))
        {
          return make_error(
            HTTP_STATUS_BAD_REQUEST, ccf::errors::InvalidResourceName, error);
        }

        auto proposals = ctx.tx.ro(this->network.proposals);
        auto proposal = proposals->get(proposal_id);
        if (!proposal)
        {
          return make_error(
            HTTP_STATUS_NOT_FOUND,
            ccf::errors::ProposalNotFound,
            fmt::format("Proposal {} does not exist.", proposal_id));
        }

        const auto vote_it = proposal->votes.find(vote_member_id);
        if (vote_it == proposal->votes.end())
        {
          return make_error(
            HTTP_STATUS_NOT_FOUND,
            ccf::errors::VoteNotFound,
            fmt::format(
              "Member {} has not voted for proposal {}.",
              vote_member_id,
              proposal_id));
        }

        return make_success(vote_it->second);
      };
      make_read_only_endpoint(
        "proposals/{proposal_id}/votes/{member_id}",
        HTTP_GET,
        json_read_only_adapter(get_vote),
        member_cert_or_sig)
        .set_auto_schema<void, Vote>()
        .install();

      //! A member acknowledges state
      auto ack = [this](auto& ctx, nlohmann::json&& params) {
        const auto& caller_identity =
          ctx.template get_caller<ccf::MemberSignatureAuthnIdentity>();
        const auto& signed_request = caller_identity.signed_request;

        auto mas = ctx.tx.rw(this->network.member_acks);
        const auto ma = mas->get(caller_identity.member_id);
        if (!ma)
        {
          return make_error(
            HTTP_STATUS_FORBIDDEN,
            ccf::errors::AuthorizationFailed,
            fmt::format(
              "No ACK record exists for caller {}.",
              caller_identity.member_id));
        }

        const auto digest = params.get<StateDigest>();
        if (ma->state_digest != digest.state_digest)
        {
          return make_error(
            HTTP_STATUS_BAD_REQUEST,
            ccf::errors::StateDigestMismatch,
            "Submitted state digest is not valid.");
        }

        auto sig = ctx.tx.rw(this->network.signatures);
        const auto s = sig->get(0);
        if (!s)
        {
          mas->put(caller_identity.member_id, MemberAck({}, signed_request));
        }
        else
        {
          mas->put(
            caller_identity.member_id, MemberAck(s->root, signed_request));
        }

        // update member status to ACTIVE
        GenesisGenerator g(this->network, ctx.tx);
        try
        {
          g.activate_member(caller_identity.member_id);
        }
        catch (const std::logic_error& e)
        {
          return make_error(
            HTTP_STATUS_FORBIDDEN,
            ccf::errors::AuthorizationFailed,
            fmt::format("Error activating new member: {}", e.what()));
        }

        auto service_status = g.get_service_status();
        if (!service_status.has_value())
        {
          return make_error(
            HTTP_STATUS_INTERNAL_SERVER_ERROR,
            ccf::errors::InternalError,
            "No service currently available.");
        }

        auto members = ctx.tx.rw(this->network.member_info);
        auto member_info = members->get(caller_identity.member_id);
        if (
          service_status.value() == ServiceStatus::OPEN &&
          g.is_recovery_member(caller_identity.member_id))
        {
          // When the service is OPEN and the new active member is a recovery
          // member, all recovery members are allocated new recovery shares
          try
          {
            share_manager.shuffle_recovery_shares(ctx.tx);
          }
          catch (const std::logic_error& e)
          {
            return make_error(
              HTTP_STATUS_INTERNAL_SERVER_ERROR,
              ccf::errors::InternalError,
              fmt::format("Error issuing new recovery shares: {}", e.what()));
          }
        }
        return make_success();
      };
      make_endpoint("ack", HTTP_POST, json_adapter(ack), member_sig_only)
        .set_auto_schema<StateDigest, void>()
        .install();

      //! A member asks for a fresher state digest
      auto update_state_digest = [this](auto& ctx, nlohmann::json&&) {
        const auto member_id = get_caller_member_id(ctx);
        if (!member_id.has_value())
        {
          return make_error(
            HTTP_STATUS_FORBIDDEN,
            ccf::errors::AuthorizationFailed,
            "Caller is a not a valid member id");
        }

        auto mas = ctx.tx.rw(this->network.member_acks);
        auto sig = ctx.tx.rw(this->network.signatures);
        auto ma = mas->get(member_id.value());
        if (!ma)
        {
          return make_error(
            HTTP_STATUS_FORBIDDEN,
            ccf::errors::AuthorizationFailed,
            fmt::format(
              "No ACK record exists for caller {}.", member_id.value()));
        }

        auto s = sig->get(0);
        if (s)
        {
          ma->state_digest = s->root.hex_str();
          mas->put(member_id.value(), ma.value());
        }
        nlohmann::json j;
        j["state_digest"] = ma->state_digest;

        return make_success(j);
      };
      make_endpoint(
        "ack/update_state_digest",
        HTTP_POST,
        json_adapter(update_state_digest),
        member_cert_or_sig)
        .set_auto_schema<void, StateDigest>()
        .install();

      auto get_encrypted_recovery_share = [this](auto& ctx, nlohmann::json&&) {
        const auto member_id = get_caller_member_id(ctx);
        if (!member_id.has_value())
        {
          return make_error(
            HTTP_STATUS_FORBIDDEN,
            ccf::errors::AuthorizationFailed,
            "Member is unknown.");
        }
        if (!check_member_active(ctx.tx, member_id.value()))
        {
          return make_error(
            HTTP_STATUS_FORBIDDEN,
            ccf::errors::AuthorizationFailed,
            "Only active members are given recovery shares.");
        }

        auto encrypted_share =
          share_manager.get_encrypted_share(ctx.tx, member_id.value());

        if (!encrypted_share.has_value())
        {
          return make_error(
            HTTP_STATUS_NOT_FOUND,
            ccf::errors::ResourceNotFound,
            fmt::format(
              "Recovery share not found for member {}.", member_id->value()));
        }

        return make_success(
          GetRecoveryShare::Out{tls::b64_from_raw(encrypted_share.value())});
      };
      make_endpoint(
        "recovery_share",
        HTTP_GET,
        json_adapter(get_encrypted_recovery_share),
        member_cert_or_sig)
        .set_auto_schema<GetRecoveryShare>()
        .install();

      auto submit_recovery_share = [this](auto& ctx, nlohmann::json&& params) {
        // Only active members can submit their shares for recovery
        const auto member_id = get_caller_member_id(ctx);
        if (!member_id.has_value())
        {
          return make_error(
            HTTP_STATUS_FORBIDDEN,
            ccf::errors::AuthorizationFailed,
            "Member is unknown.");
        }
        if (!check_member_active(ctx.tx, member_id.value()))
        {
          return make_error(
            HTTP_STATUS_FORBIDDEN,
            errors::AuthorizationFailed,
            "Member is not active");
        }

        GenesisGenerator g(this->network, ctx.tx);
        if (
          g.get_service_status() != ServiceStatus::WAITING_FOR_RECOVERY_SHARES)
        {
          return make_error(
            HTTP_STATUS_FORBIDDEN,
            errors::ServiceNotWaitingForRecoveryShares,
            "Service is not waiting for recovery shares");
        }

        if (context.get_node_state().is_reading_private_ledger())
        {
          return make_error(
            HTTP_STATUS_FORBIDDEN,
            errors::NodeAlreadyRecovering,
            "Node is already recovering private ledger");
        }

        const auto in = params.get<SubmitRecoveryShare::In>();
        auto raw_recovery_share = tls::raw_from_b64(in.share);

        size_t submitted_shares_count = 0;
        try
        {
          submitted_shares_count = share_manager.submit_recovery_share(
            ctx.tx, member_id.value(), raw_recovery_share);
        }
        catch (const std::exception& e)
        {
          constexpr auto error_msg = "Error submitting recovery shares";
          LOG_FAIL_FMT(error_msg);
          LOG_DEBUG_FMT("Error: {}", e.what());
          return make_error(
            HTTP_STATUS_INTERNAL_SERVER_ERROR,
            errors::InternalError,
            error_msg);
        }

        if (submitted_shares_count < g.get_recovery_threshold())
        {
          // The number of shares required to re-assemble the secret has not yet
          // been reached
          return make_success(SubmitRecoveryShare::Out{fmt::format(
            "{}/{} recovery shares successfully submitted.",
            submitted_shares_count,
            g.get_recovery_threshold())});
        }

        LOG_DEBUG_FMT(
          "Reached recovery threshold {}", g.get_recovery_threshold());

        try
        {
          context.get_node_state().initiate_private_recovery(ctx.tx);
        }
        catch (const std::exception& e)
        {
          // Clear the submitted shares if combination fails so that members can
          // start over.
          constexpr auto error_msg = "Failed to initiate private recovery";
          LOG_FAIL_FMT(error_msg);
          LOG_DEBUG_FMT("Error: {}", e.what());
          share_manager.clear_submitted_recovery_shares(ctx.tx);
          ctx.rpc_ctx->set_apply_writes(true);
          return make_error(
            HTTP_STATUS_INTERNAL_SERVER_ERROR,
            errors::InternalError,
            error_msg);
        }

        share_manager.clear_submitted_recovery_shares(ctx.tx);

        return make_success(SubmitRecoveryShare::Out{fmt::format(
          "{}/{} recovery shares successfully submitted. End of recovery "
          "procedure initiated.",
          submitted_shares_count,
          g.get_recovery_threshold())});
      };
      make_endpoint(
        "recovery_share",
        HTTP_POST,
        json_adapter(submit_recovery_share),
        member_cert_or_sig)
        .set_auto_schema<SubmitRecoveryShare>()
        .install();

      auto create = [this](auto& ctx, nlohmann::json&& params) {
        LOG_DEBUG_FMT("Processing create RPC");
        const auto in = params.get<CreateNetworkNodeToNode::In>();

        GenesisGenerator g(this->network, ctx.tx);

        // This endpoint can only be called once, directly from the starting
        // node for the genesis transaction to initialise the service
        if (g.is_service_created())
        {
          return make_error(
            HTTP_STATUS_INTERNAL_SERVER_ERROR,
            ccf::errors::InternalError,
            "Service is already created.");
        }

        g.init_values();
        g.create_service(in.network_cert);

        for (const auto& info : in.members_info)
        {
          g.add_member(info);
        }

        // Note that it is acceptable to start a network without any member
        // having a recovery share. The service will check that at least one
        // recovery member is added before the service is opened.
        g.init_configuration(in.configuration);

        g.add_node(
          in.node_id,
          {in.node_info_network,
           in.node_cert,
           {in.quote_info},
           in.public_encryption_key,
           NodeStatus::TRUSTED});

#ifdef GET_QUOTE
        g.trust_node_code_id(in.code_digest);
#endif

        for (const auto& wl : default_whitelists)
        {
          g.set_whitelist(wl.first, wl.second);
        }

        g.set_gov_scripts(
          lua::Interpreter().invoke<nlohmann::json>(in.gov_script));

        ctx.tx.rw(this->network.constitution)->put(0, in.constitution);

        LOG_INFO_FMT("Created service");
        return make_success(true);
      };
      make_endpoint("create", HTTP_POST, json_adapter(create), no_auth_required)
        .set_openapi_hidden(true)
        .install();

      // Only called from node. See node_state.h.
      auto refresh_jwt_keys = [this](auto& ctx, nlohmann::json&& body) {
        // All errors are server errors since the client is the server.

        if (!consensus)
        {
          LOG_FAIL_FMT("JWT key auto-refresh: no consensus available");
          return make_error(
            HTTP_STATUS_INTERNAL_SERVER_ERROR,
            ccf::errors::InternalError,
            "No consensus available.");
        }

        auto primary_id = consensus->primary();
        if (!primary_id.has_value())
        {
          LOG_FAIL_FMT("JWT key auto-refresh: primary unknown");
          return make_error(
            HTTP_STATUS_INTERNAL_SERVER_ERROR,
            ccf::errors::InternalError,
            "Primary is unknown");
        }

        const auto& cert_auth_ident =
          ctx.template get_caller<ccf::NodeCertAuthnIdentity>();
        if (primary_id.value() != cert_auth_ident.node_id)
        {
          LOG_FAIL_FMT(
            "JWT key auto-refresh: request does not originate from primary");
          return make_error(
            HTTP_STATUS_INTERNAL_SERVER_ERROR,
            ccf::errors::InternalError,
            "Request does not originate from primary.");
        }

        SetJwtPublicSigningKeys parsed;
        try
        {
          parsed = body.get<SetJwtPublicSigningKeys>();
        }
        catch (const JsonParseError& e)
        {
          return make_error(
            HTTP_STATUS_INTERNAL_SERVER_ERROR,
            ccf::errors::InternalError,
            "Unable to parse body.");
        }

        auto issuers = ctx.tx.rw(this->network.jwt_issuers);
        auto issuer_metadata_ = issuers->get(parsed.issuer);
        if (!issuer_metadata_.has_value())
        {
          LOG_FAIL_FMT(
            "JWT key auto-refresh: {} is not a valid issuer", parsed.issuer);
          return make_error(
            HTTP_STATUS_INTERNAL_SERVER_ERROR,
            ccf::errors::InternalError,
            fmt::format("{} is not a valid issuer.", parsed.issuer));
        }
        auto& issuer_metadata = issuer_metadata_.value();

        if (!issuer_metadata.auto_refresh)
        {
          LOG_FAIL_FMT(
            "JWT key auto-refresh: {} does not have auto_refresh enabled",
            parsed.issuer);
          return make_error(
            HTTP_STATUS_INTERNAL_SERVER_ERROR,
            ccf::errors::InternalError,
            fmt::format(
              "{} does not have auto_refresh enabled.", parsed.issuer));
        }

        if (!set_jwt_public_signing_keys(
              ctx.tx,
              "",
              parsed.issuer,
              issuer_metadata,
              parsed.jwks))
        {
          LOG_FAIL_FMT(
            "JWT key auto-refresh: error while storing signing keys for issuer "
            "{}",
            parsed.issuer);
          return make_error(
            HTTP_STATUS_INTERNAL_SERVER_ERROR,
            ccf::errors::InternalError,
            fmt::format(
              "Error while storing signing keys for issuer {}.",
              parsed.issuer));
        }

        return make_success(true);
      };
      make_endpoint(
        "jwt_keys/refresh",
        HTTP_POST,
        json_adapter(refresh_jwt_keys),
        {std::make_shared<NodeCertAuthnPolicy>()})
        .set_openapi_hidden(true)
        .install();

      // JavaScript governance
#ifdef ENABLE_JS_GOV

#  pragma clang diagnostic push
#  pragma clang diagnostic ignored "-Wc99-extensions"

      auto post_proposals_js = [this](ccf::endpoints::EndpointContext& ctx) {
        const auto& caller_identity =
          ctx.get_caller<ccf::MemberSignatureAuthnIdentity>();
        if (!check_member_active(ctx.tx, caller_identity.member_id))
        {
          ctx.rpc_ctx->set_error(
            HTTP_STATUS_FORBIDDEN,
            ccf::errors::AuthorizationFailed,
            "Member is not active.");
          return;
        }

        if (!consensus)
        {
          ctx.rpc_ctx->set_error(
            HTTP_STATUS_INTERNAL_SERVER_ERROR,
            ccf::errors::InternalError,
            "No consensus available.");
          return;
        }

        ProposalId proposal_id;
        if (consensus->type() == ConsensusType::CFT)
        {
          auto root_at_read = ctx.tx.get_root_at_read_version();
          if (!root_at_read.has_value())
          {
            ctx.rpc_ctx->set_error(
              HTTP_STATUS_INTERNAL_SERVER_ERROR,
              ccf::errors::InternalError,
              "Proposal failed to bind to state.");
            return;
          }

          // caller_identity.request_digest is set when getting the
          // MemberSignatureAuthnIdentity identity. The proposal id is a
          // digest of the root of the state tree at the read version and the
          // request digest.
          std::vector<uint8_t> acc(
            root_at_read.value().h.begin(), root_at_read.value().h.end());
          acc.insert(
            acc.end(),
            caller_identity.request_digest.begin(),
            caller_identity.request_digest.end());
          const crypto::Sha256Hash proposal_digest(acc);
          proposal_id = proposal_digest.hex_str();
        }
        else
        {
          proposal_id = fmt::format(
            "{:02x}", fmt::join(caller_identity.request_digest, ""));
        }

        js::Runtime rt;
        js::Context context(rt);
        auto constitution = ctx.tx.ro(network.constitution)->get(0);
        if (!constitution.has_value())
        {
          ctx.rpc_ctx->set_error(
            HTTP_STATUS_INTERNAL_SERVER_ERROR,
            ccf::errors::InternalError,
            "No constitution is set - proposals cannot be evaluated");
          return;
        }

        auto validate_script = fmt::format(
          "{}\n export default (input) => validate(input);",
          constitution.value());

        auto validate_func = context.function(
          validate_script, "public:ccf.gov.constitution[0].validate");

        auto body =
          reinterpret_cast<const char*>(ctx.rpc_ctx->get_request_body().data());
        auto body_len = ctx.rpc_ctx->get_request_body().size();

        auto proposal = JS_NewStringLen(context, body, body_len);
        JSValueConst* argv = (JSValueConst*)&proposal;

        auto val =
          context(JS_Call(context, validate_func, JS_UNDEFINED, 1, argv));

        JS_FreeValue(context, proposal);
        JS_FreeValue(context, validate_func);

        if (JS_IsException(val))
        {
          js::js_dump_error(context);
          ctx.rpc_ctx->set_error(
            HTTP_STATUS_INTERNAL_SERVER_ERROR,
            ccf::errors::InternalError,
            "Failed to execute validation");
          return;
        }

        if (!JS_IsObject(val))
        {
          ctx.rpc_ctx->set_error(
            HTTP_STATUS_INTERNAL_SERVER_ERROR,
            ccf::errors::InternalError,
            "Validation failed to return an object");
          return;
        }

        std::string description;
        auto desc = context(JS_GetPropertyStr(context, val, "description"));
        if (JS_IsString(desc))
        {
          auto cstr = JS_ToCString(context, desc);
          description = std::string(cstr);
          JS_FreeCString(context, cstr);
        }

        auto valid = context(JS_GetPropertyStr(context, val, "valid"));
        if (!JS_ToBool(context, valid))
        {
          ctx.rpc_ctx->set_error(
            HTTP_STATUS_BAD_REQUEST,
            ccf::errors::ProposalFailedToValidate,
            fmt::format("Proposal failed to validate: {}", description));
          return;
        }

        auto pm =
          ctx.tx.rw<ccf::jsgov::ProposalMap>("public:ccf.gov.proposals.js");
        // Introduce a read dependency, so that if identical proposal
        // creations are in-flight and reading at the same version, all except
        // the first conflict and are re-executed. If we ever produce a
        // proposal ID which already exists, we must have a hash collision.
        if (pm->has(proposal_id))
        {
          ctx.rpc_ctx->set_error(
            HTTP_STATUS_INTERNAL_SERVER_ERROR,
            ccf::errors::InternalError,
            "Proposal ID collision.");
          return;
        }
        pm->put(proposal_id, ctx.rpc_ctx->get_request_body());

        auto pi = ctx.tx.rw<ccf::jsgov::ProposalInfoMap>(
          "public:ccf.gov.proposals_info.js");
        pi->put(
          proposal_id,
          {caller_identity.member_id, ccf::ProposalState::OPEN, {}});

        record_voting_history(
          ctx.tx, caller_identity.member_id, caller_identity.signed_request);

        auto rv = resolve_proposal(
          ctx.tx,
          proposal_id,
          ctx.rpc_ctx->get_request_body(),
          constitution.value());
        pi->put(proposal_id, {caller_identity.member_id, rv.state, {}});

        ctx.rpc_ctx->set_response_status(HTTP_STATUS_OK);
        ctx.rpc_ctx->set_response_header(
          http::headers::CONTENT_TYPE, http::headervalues::contenttype::JSON);
        ctx.rpc_ctx->set_response_body(nlohmann::json(rv).dump());
      };

      make_endpoint(
        "proposals.js", HTTP_POST, post_proposals_js, member_sig_only)
        .set_auto_schema<jsgov::Proposal, jsgov::ProposalInfo>()
        .install();

      auto get_proposal_js =
        [this](endpoints::ReadOnlyEndpointContext& ctx, nlohmann::json&&) {
          const auto& caller_identity =
            ctx.get_caller<ccf::MemberSignatureAuthnIdentity>();
          if (!check_member_active(ctx.tx, caller_identity.member_id))
          {
            return make_error(
              HTTP_STATUS_FORBIDDEN,
              ccf::errors::AuthorizationFailed,
              "Member is not active.");
          }

          // Take expand=ballots, return eg. "ballots": 3 if not set
          // or "ballots": list of ballots in full if passed

          ProposalId proposal_id;
          std::string error;
          if (!get_proposal_id_from_path(
                ctx.rpc_ctx->get_request_path_params(), proposal_id, error))
          {
            return make_error(
              HTTP_STATUS_BAD_REQUEST, ccf::errors::InvalidResourceName, error);
          }

          auto pm =
            ctx.tx.ro<ccf::jsgov::ProposalMap>("public:ccf.gov.proposals.js");
          auto p = pm->get(proposal_id);

          if (!p)
          {
            return make_error(
              HTTP_STATUS_NOT_FOUND,
              ccf::errors::ProposalNotFound,
              fmt::format("Proposal {} does not exist.", proposal_id));
          }

          auto pi = ctx.tx.ro<ccf::jsgov::ProposalInfoMap>(
            "public:ccf.gov.proposals_info.js");
          auto pi_ = pi->get(proposal_id);

          if (!pi_)
          {
            return make_error(
              HTTP_STATUS_INTERNAL_SERVER_ERROR,
              ccf::errors::InternalError,
              fmt::format(
                "No proposal info associated with {} exists.", proposal_id));
          }

          return make_success(pi_.value());
        };

      make_read_only_endpoint(
        "proposals.js/{proposal_id}",
        HTTP_GET,
        json_read_only_adapter(get_proposal_js),
        member_cert_or_sig)
        .set_auto_schema<void, jsgov::ProposalInfo>()
        .install();

      auto withdraw_js = [this](
                           endpoints::EndpointContext& ctx, nlohmann::json&&) {
        const auto& caller_identity =
          ctx.template get_caller<ccf::MemberSignatureAuthnIdentity>();
        if (!check_member_active(ctx.tx, caller_identity.member_id))
        {
          return make_error(
            HTTP_STATUS_FORBIDDEN,
            ccf::errors::AuthorizationFailed,
            "Member is not active.");
        }

        ProposalId proposal_id;
        std::string error;
        if (!get_proposal_id_from_path(
              ctx.rpc_ctx->get_request_path_params(), proposal_id, error))
        {
          return make_error(
            HTTP_STATUS_BAD_REQUEST, ccf::errors::InvalidResourceName, error);
        }

        auto pi = ctx.tx.rw<ccf::jsgov::ProposalInfoMap>(
          "public:ccf.gov.proposals_info.js");
        auto pi_ = pi->get(proposal_id);

        if (!pi_)
        {
          return make_error(
            HTTP_STATUS_BAD_REQUEST,
            ccf::errors::ProposalNotFound,
            fmt::format("Proposal {} does not exist.", proposal_id));
        }

        if (caller_identity.member_id != pi_->proposer_id)
        {
          return make_error(
            HTTP_STATUS_FORBIDDEN,
            ccf::errors::AuthorizationFailed,
            fmt::format(
              "Proposal {} can only be withdrawn by proposer {}, not caller "
              "{}.",
              proposal_id,
              pi_->proposer_id,
              caller_identity.member_id));
        }

        if (pi_->state != ProposalState::OPEN)
        {
          return make_error(
            HTTP_STATUS_BAD_REQUEST,
            ccf::errors::ProposalNotOpen,
            fmt::format(
              "Proposal {} is currently in state {} - only {} proposals can be "
              "withdrawn.",
              proposal_id,
              pi_->state,
              ProposalState::OPEN));
        }

        pi_->state = ProposalState::WITHDRAWN;
        pi->put(proposal_id, pi_.value());

        record_voting_history(
          ctx.tx, caller_identity.member_id, caller_identity.signed_request);

        return make_success(pi_.value());
      };

      make_endpoint(
        "proposals.js/{proposal_id}/withdraw",
        HTTP_POST,
        json_adapter(withdraw_js),
        member_cert_or_sig)
        .set_auto_schema<void, jsgov::ProposalInfo>()
        .install();

      auto get_proposal_actions_js =
        [this](ccf::endpoints::ReadOnlyEndpointContext& ctx) {
          const auto& caller_identity =
            ctx.get_caller<ccf::MemberSignatureAuthnIdentity>();
          if (!check_member_active(ctx.tx, caller_identity.member_id))
          {
            ctx.rpc_ctx->set_error(
              HTTP_STATUS_FORBIDDEN,
              ccf::errors::AuthorizationFailed,
              "Member is not active.");
            return;
          }

          ProposalId proposal_id;
          std::string error;
          if (!get_proposal_id_from_path(
                ctx.rpc_ctx->get_request_path_params(), proposal_id, error))
          {
            ctx.rpc_ctx->set_error(
              HTTP_STATUS_BAD_REQUEST,
              ccf::errors::InvalidResourceName,
              std::move(error));
            return;
          }

          auto pm =
            ctx.tx.ro<ccf::jsgov::ProposalMap>("public:ccf.gov.proposals.js");
          auto p = pm->get(proposal_id);

          if (!p)
          {
            ctx.rpc_ctx->set_error(
              HTTP_STATUS_NOT_FOUND,
              ccf::errors::ProposalNotFound,
              fmt::format("Proposal {} does not exist.", proposal_id));
            return;
          }

          ctx.rpc_ctx->set_response_status(HTTP_STATUS_OK);
          ctx.rpc_ctx->set_response_header(
            http::headers::CONTENT_TYPE, http::headervalues::contenttype::JSON);
          ctx.rpc_ctx->set_response_body(std::move(p.value()));
        };

      make_read_only_endpoint(
        "proposals.js/{proposal_id}/actions",
        HTTP_GET,
        get_proposal_actions_js,
        member_cert_or_sig)
        .set_auto_schema<void, jsgov::Proposal>()
        .install();

      auto vote_js = [this](
                       endpoints::EndpointContext& ctx,
                       nlohmann::json&& params) {
        const auto& caller_identity =
          ctx.get_caller<ccf::MemberSignatureAuthnIdentity>();
        if (!check_member_active(ctx.tx, caller_identity.member_id))
        {
          return make_error(
            HTTP_STATUS_FORBIDDEN,
            ccf::errors::AuthorizationFailed,
            "Member is not active.");
        }

        ProposalId proposal_id;
        std::string error;
        if (!get_proposal_id_from_path(
              ctx.rpc_ctx->get_request_path_params(), proposal_id, error))
        {
          return make_error(
            HTTP_STATUS_BAD_REQUEST, ccf::errors::InvalidResourceName, error);
        }

        auto constitution = ctx.tx.ro(network.constitution)->get(0);
        if (!constitution.has_value())
        {
          return make_error(
            HTTP_STATUS_INTERNAL_SERVER_ERROR,
            ccf::errors::InternalError,
            "No constitution is set - proposals cannot be evaluated");
        }

        auto pi = ctx.tx.rw<ccf::jsgov::ProposalInfoMap>(
          "public:ccf.gov.proposals_info.js");
        auto pi_ = pi->get(proposal_id);
        if (!pi_)
        {
          return make_error(
            HTTP_STATUS_NOT_FOUND,
            ccf::errors::ProposalNotFound,
            fmt::format("Could not find proposal {}.", proposal_id));
        }

        if (pi_.value().state != ProposalState::OPEN)
        {
          return make_error(
            HTTP_STATUS_BAD_REQUEST,
            ccf::errors::ProposalNotOpen,
            fmt::format(
              "Proposal {} is currently in state {} - only {} proposals can "
              "receive votes.",
              proposal_id,
              pi_.value().state,
              ProposalState::OPEN));
        }

        auto pm =
          ctx.tx.ro<ccf::jsgov::ProposalMap>("public:ccf.gov.proposals.js");
        auto p = pm->get(proposal_id);

        if (!p)
        {
          return make_error(
            HTTP_STATUS_NOT_FOUND,
            ccf::errors::ProposalNotFound,
            fmt::format("Proposal {} does not exist.", proposal_id));
        }

        if (pi_->ballots.find(caller_identity.member_id) != pi_->ballots.end())
        {
          return make_error(
            HTTP_STATUS_BAD_REQUEST,
            ccf::errors::VoteAlreadyExists,
            "Vote already submitted.");
        }
        // Validate vote

        std::string ballot_script = fmt::format(
          "{}\n export default (proposal, proposer_id, tx) => vote(proposal, "
          "proposer_id, tx);",
          params["ballot"]);

        {
          js::Runtime rt;
          js::Context context(rt);
          auto ballot_func =
            context.function(ballot_script, "body[\"ballot\"]");
          JS_FreeValue(context, ballot_func);
        }

        pi_->ballots[caller_identity.member_id] = params["ballot"];
        pi->put(proposal_id, pi_.value());

        // Do we still need to do this?
        record_voting_history(
          ctx.tx, caller_identity.member_id, caller_identity.signed_request);

        auto rv = resolve_proposal(
          ctx.tx, proposal_id, p.value(), constitution.value());
        pi_.value().state = rv.state;
        pi->put(proposal_id, pi_.value());
        return make_success(rv);
      };
      make_endpoint(
        "proposals.js/{proposal_id}/ballots",
        HTTP_POST,
        json_adapter(vote_js),
        member_sig_only)
        .set_auto_schema<jsgov::Ballot, jsgov::ProposalInfoSummary>()
        .install();

      auto get_vote_js =
        [this](endpoints::ReadOnlyEndpointContext& ctx, nlohmann::json&&) {
          const auto& caller_identity =
            ctx.get_caller<ccf::MemberSignatureAuthnIdentity>();
          if (!check_member_active(ctx.tx, caller_identity.member_id))
          {
            return make_error(
              HTTP_STATUS_FORBIDDEN,
              ccf::errors::AuthorizationFailed,
              "Member is not active.");
          }

          std::string error;
          ProposalId proposal_id;
          if (!get_proposal_id_from_path(
                ctx.rpc_ctx->get_request_path_params(), proposal_id, error))
          {
            return make_error(
              HTTP_STATUS_BAD_REQUEST, ccf::errors::InvalidResourceName, error);
          }

          MemberId vote_member_id;
          if (!get_member_id_from_path(
                ctx.rpc_ctx->get_request_path_params(), vote_member_id, error))
          {
            return make_error(
              HTTP_STATUS_BAD_REQUEST, ccf::errors::InvalidResourceName, error);
          }

          auto pi = ctx.tx.ro<ccf::jsgov::ProposalInfoMap>(
            "public:ccf.gov.proposals_info.js");
          auto pi_ = pi->get(proposal_id);
          if (!pi_)
          {
            return make_error(
              HTTP_STATUS_NOT_FOUND,
              ccf::errors::ProposalNotFound,
              fmt::format("Proposal {} does not exist.", proposal_id));
          }

          const auto vote_it = pi_->ballots.find(vote_member_id);
          if (vote_it == pi_->ballots.end())
          {
            return make_error(
              HTTP_STATUS_NOT_FOUND,
              ccf::errors::VoteNotFound,
              fmt::format(
                "Member {} has not voted for proposal {}.",
                vote_member_id,
                proposal_id));
          }

          return make_success(jsgov::Ballot{vote_it->second});
        };
      make_read_only_endpoint(
        "proposals.js/{proposal_id}/ballots/{member_id}",
        HTTP_GET,
        json_read_only_adapter(get_vote_js),
        member_cert_or_sig)
        .set_auto_schema<void, jsgov::Ballot>()
        .install();

#  pragma clang diagnostic pop

#endif
    }
  };

  class MemberRpcFrontend : public RpcFrontend
  {
  protected:
    MemberEndpoints member_endpoints;

  public:
    MemberRpcFrontend(
      NetworkState& network,
      ccfapp::AbstractNodeContext& context,
      ShareManager& share_manager) :
      RpcFrontend(*network.tables, member_endpoints),
      member_endpoints(network, context, share_manager)
    {}
  };
} // namespace ccf
