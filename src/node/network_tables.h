// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#pragma once
#include "backup_signatures.h"
#include "call_types.h"
#include "certs.h"
#include "client_signatures.h"
#include "code_id.h"
#include "config.h"
#include "consensus.h"
#include "consensus/aft/raft_tables.h"
#include "consensus/aft/request.h"
#include "consensus/aft/revealed_nonces.h"
#include "entities.h"
#include "governance_history.h"
#include "jwt.h"
#include "kv/map.h"
#include "kv/store.h"
#include "members.h"
#include "modules.h"
#include "nodes.h"
#include "proposals.h"
#include "scripts.h"
#include "secrets.h"
#include "service.h"
#include "shares.h"
#include "signatures.h"
#include "snapshot_evidence.h"
#include "submitted_shares.h"
#include "users.h"
#include "values.h"
#include "whitelists.h"

#include <memory>
#include <tuple>

namespace ccf
{
  struct NetworkTables
  {
    std::shared_ptr<kv::Store> tables;

    //
    // Governance tables
    //
    // members and member_certs tables should always be in sync
    Members members;
    CertDERs member_certs;

    Scripts gov_scripts;
    Modules modules;
    Proposals proposals;
    Whitelists whitelists;
    CodeIDs node_code_ids;
    MemberAcks member_acks;
    GovernanceHistory governance_history;
    ClientSignatures member_client_signatures;
    Shares shares;
    SubmittedShares submitted_shares;
    Configuration config;

    CACertDERs ca_certs;

    JwtIssuers jwt_issuers;
    JwtIssuerKeyIds jwt_issuer_key_ids;
    JwtPublicSigningKeys jwt_public_signing_keys;
    JwtPublicSigningKeysValidateIssuer jwt_public_signing_keys_validate_issuer;

    //
    // User tables
    //
    // users and user_certs tables should always be in sync
    Users users;
    CertDERs user_certs;

    ClientSignatures user_client_signatures;

    //
    // Node table
    //
    Nodes nodes;

    //
    // Lua application table
    //
    Scripts app_scripts;

    //
    // Internal CCF tables
    //
    Service service;
    Values values;
    Secrets secrets;
    Signatures signatures;
    ConsensusTable consensus;
    SnapshotEvidence snapshot_evidence;

    //
    // bft related tables
    //
    aft::RequestsMap bft_requests_map;
    BackupSignaturesMap backup_signatures_map;
    aft::RevealedNoncesMap revealed_nonces_map;

    NetworkTables(const ConsensusType& consensus_type = ConsensusType::CFT) :
      tables(
        (consensus_type == ConsensusType::CFT) ?
          std::make_shared<kv::Store>(
            aft::replicate_type_raft, aft::replicated_tables_raft) :
          std::make_shared<kv::Store>(
            aft::replicate_type_bft, aft::replicated_tables_bft)),
      members(Tables::MEMBERS),
      member_certs(Tables::MEMBER_CERT_DERS),
      gov_scripts(Tables::GOV_SCRIPTS),
      modules(Tables::MODULES),
      proposals(Tables::PROPOSALS),
      whitelists(Tables::WHITELISTS),
      node_code_ids(Tables::NODE_CODE_IDS),
      member_acks(Tables::MEMBER_ACKS),
      governance_history(Tables::GOV_HISTORY),
      member_client_signatures(Tables::MEMBER_CLIENT_SIGNATURES),
      shares(Tables::SHARES),
      submitted_shares(Tables::SUBMITTED_SHARES),
      config(Tables::CONFIGURATION),
      ca_certs(Tables::CA_CERT_DERS),
      jwt_issuers(Tables::JWT_ISSUERS),
      jwt_issuer_key_ids(Tables::JWT_ISSUER_KEY_IDS),
      jwt_public_signing_keys(Tables::JWT_PUBLIC_SIGNING_KEYS),
      jwt_public_signing_keys_validate_issuer(Tables::JWT_PUBLIC_SIGNING_KEYS_VALIDATE_ISSUER),
      users(Tables::USERS),
      user_certs(Tables::USER_CERT_DERS),
      user_client_signatures(Tables::USER_CLIENT_SIGNATURES),
      nodes(Tables::NODES),
      app_scripts(Tables::APP_SCRIPTS),
      service(Tables::SERVICE),
      values(Tables::VALUES),
      secrets(Tables::SECRETS),
      signatures(Tables::SIGNATURES),
      consensus(Tables::CONSENSUS),
      snapshot_evidence(Tables::SNAPSHOT_EVIDENCE),
      bft_requests_map(Tables::AFT_REQUESTS),
      backup_signatures_map(Tables::BACKUP_SIGNATURES),
      revealed_nonces_map(Tables::NONCES)
    {}

    /** Returns a tuple of all tables that are possibly accessible from scripts
     * (app and gov). More fine-grained access control is applied via
     * whitelists.
     */
    auto get_scriptable_tables() const
    {
      return std::make_tuple(
        std::ref(members),
        std::ref(member_certs),
        std::ref(gov_scripts),
        std::ref(modules),
        std::ref(proposals),
        std::ref(whitelists),
        std::ref(node_code_ids),
        std::ref(member_acks),
        std::ref(governance_history),
        std::ref(member_client_signatures),
        std::ref(config),
        std::ref(ca_certs),
        std::ref(jwt_issuers),
        std::ref(jwt_issuer_key_ids),
        std::ref(jwt_public_signing_keys),
        std::ref(jwt_public_signing_keys_validate_issuer),
        std::ref(users),
        std::ref(user_certs),
        std::ref(user_client_signatures),
        std::ref(nodes),
        std::ref(service),
        std::ref(app_scripts),
        std::ref(values),
        std::ref(signatures));
    }
  };
}