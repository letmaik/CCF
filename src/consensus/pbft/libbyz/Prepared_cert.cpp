// Copyright (c) Microsoft Corporation.
// Copyright (c) 1999 Miguel Castro, Barbara Liskov.
// Copyright (c) 2000, 2001 Miguel Castro, Rodrigo Rodrigues, Barbara Liskov.
// Licensed under the MIT license.

#include "Prepared_cert.h"

#include "Node.h"

Prepared_cert::Prepared_cert() :
  prepare_cert(node->num_correct_replicas() - 1),
  primary(false)
{}

Prepared_cert::~Prepared_cert()
{
  pp_info.clear();
}

bool Prepared_cert::is_pp_correct()
{
  if (pp_info.pre_prepare())
  {
    Certificate<Prepare>::Val_iter viter(&prepare_cert);
    int vc;
    Prepare* val;
    while (viter.get(val, vc))
    {
      if (vc >= node->f() && pp_info.pre_prepare()->match(val))
      {
        return true;
      }
    }
  }
  return false;
}

bool Prepared_cert::add(Pre_prepare* m)
{
  if (pp_info.pre_prepare() == 0)
  {
    Prepare* p = prepare_cert.mine();

    if (p == 0)
    {
      if (m->verify())
      {
        pp_info.add(m);
        return true;
      }

      if (m->verify(Pre_prepare::NRC))
      {
        // Check if there is some value that matches pp and has f
        // senders.
        Certificate<Prepare>::Val_iter viter(&prepare_cert);
        int vc;
        Prepare* val;
        while (viter.get(val, vc))
        {
          if (vc >= node->f() && m->match(val))
          {
            pp_info.add(m);
            return true;
          }
        }
      }
    }
    else
    {
      // If we sent a prepare, we only accept a matching pre-prepare.
      if (m->match(p) && m->verify(Pre_prepare::NRC))
      {
        pp_info.add(m);
        return true;
      }
    }
  }
  delete m;
  return false;
}

bool Prepared_cert::encode(FILE* o)
{
  bool ret = prepare_cert.encode(o);
  ret &= pp_info.encode(o);
  int sz = fwrite(&primary, sizeof(bool), 1, o);
  return ret & (sz == 1);
}

bool Prepared_cert::decode(FILE* i)
{
// TODO(#pbft): stub out, INSIDE_ENCLAVE
#ifndef INSIDE_ENCLAVE
  PBFT_ASSERT(pp_info.pre_prepare() == 0, "Invalid state");

  bool ret = prepare_cert.decode(i);
  ret &= pp_info.decode(i);
  int sz = fread(&primary, sizeof(bool), 1, i);
  t_sent = zero_time();

  return ret & (sz == 1);
#else
  return true;
#endif
}

void Prepared_cert::dump_state(std::ostream& os)
{
  os << " primary: " << primary;
  prepare_cert.dump_state(os);
  pp_info.dump_state(os);
}

bool Prepared_cert::is_empty() const
{
  return pp_info.pre_prepare() == 0 && prepare_cert.is_empty();
}

const std::unordered_map<int, Prepared_cert::PrePrepareProof>& Prepared_cert::
  get_pre_prepared_cert_proof() const
{
  return pre_prepare_proof;
}
