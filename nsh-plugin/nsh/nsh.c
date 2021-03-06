/*
 * nsh.c - nsh mapping
 *
 * Copyright (c) 2013 Cisco and/or its affiliates.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <vnet/vnet.h>
#include <vnet/plugin/plugin.h>
#include <nsh/nsh.h>

#include <vlibapi/api.h>
#include <vlibmemory/api.h>
#include <vlibsocket/api.h>

/* define message IDs */
#include <vpp-api/nsh_msg_enum.h>

/* define message structures */
#define vl_typedefs
#include <vpp-api/nsh_all_api_h.h>
#undef vl_typedefs

/* define generated endian-swappers */
#define vl_endianfun
#include <vpp-api/nsh_all_api_h.h>
#undef vl_endianfun

/* instantiate all the print functions we know about */
#define vl_print(handle, ...) vlib_cli_output (handle, __VA_ARGS__)
#define vl_printfun
#include <vpp-api/nsh_all_api_h.h>
#undef vl_printfun

/* Get the API version number */
#define vl_api_version(n,v) static u32 api_version=(v);
#include <vpp-api/nsh_all_api_h.h>
#undef vl_api_version

/*
 * A handy macro to set up a message reply.
 * Assumes that the following variables are available:
 * mp - pointer to request message
 * rmp - pointer to reply message type
 * rv - return value
 */

#define REPLY_MACRO(t)                                          \
  do {								\
    unix_shared_memory_queue_t * q =                            \
      vl_api_client_index_to_input_queue (mp->client_index);	\
    if (!q)                                                     \
      return;							\
                                                                \
    rmp = vl_msg_api_alloc (sizeof (*rmp));                     \
    rmp->_vl_msg_id = ntohs((t)+nm->msg_id_base);               \
    rmp->context = mp->context;                                 \
    rmp->retval = ntohl(rv);                                    \
                                                                \
    vl_msg_api_send_shmem (q, (u8 *)&rmp);                      \
  } while(0);

#define REPLY_MACRO2(t, body)                                   \
  do {                                                          \
    unix_shared_memory_queue_t * q;                             \
    rv = vl_msg_api_pd_handler (mp, rv);                        \
    q = vl_api_client_index_to_input_queue (mp->client_index);  \
    if (!q)                                                     \
        return;                                                 \
                                                                \
    rmp = vl_msg_api_alloc (sizeof (*rmp));                     \
    rmp->_vl_msg_id = ntohs((t)+nm->msg_id_base);               \
    rmp->context = mp->context;                                 \
    rmp->retval = ntohl(rv);                                    \
    do {body;} while (0);                                       \
    vl_msg_api_send_shmem (q, (u8 *)&rmp);                      \
  } while(0);

#define FINISH                                  \
    vec_add1 (s, 0);                            \
    vl_print (handle, (char *)s);               \
    vec_free (s);                               \
    return handle;

/* List of message types that this plugin understands */

#define foreach_nsh_plugin_api_msg		\
  _(NSH_ADD_DEL_ENTRY, nsh_add_del_entry)	\
  _(NSH_ENTRY_DUMP, nsh_entry_dump)             \
  _(NSH_ADD_DEL_MAP, nsh_add_del_map)           \
  _(NSH_MAP_DUMP, nsh_map_dump)

clib_error_t *
vlib_plugin_register (vlib_main_t * vm, vnet_plugin_handoff_t * h,
                      int from_early_init)
{
  nsh_main_t * nm = &nsh_main;
  clib_error_t * error = 0;

  nm->vlib_main = vm;
  nm->vnet_main = h->vnet_main;

  return error;
}

extern vlib_node_registration_t nsh_input_node;

typedef struct {
  nsh_header_t nsh_header;
} nsh_input_trace_t;

u8 * format_nsh_header (u8 * s, va_list * args)
{
  nsh_header_t * nsh = va_arg (*args, nsh_header_t *);

  s = format (s, "nsh ver %d ", (nsh->ver_o_c>>6));
  if (nsh->ver_o_c & NSH_O_BIT)
    s = format (s, "O-set ");

  if (nsh->ver_o_c & NSH_C_BIT)
    s = format (s, "C-set ");

  s = format (s, "len %d (%d bytes) md_type %d next_protocol %d\n",
              nsh->length, nsh->length * 4, nsh->md_type, nsh->next_protocol);

  s = format (s, "  service path %d service index %d\n",
              (nsh->nsp_nsi>>NSH_NSP_SHIFT) & NSH_NSP_MASK,
              nsh->nsp_nsi & NSH_NSI_MASK);

  s = format (s, "  c1 %d c2 %d c3 %d c4 %d\n",
              nsh->c1, nsh->c2, nsh->c3, nsh->c4);

  return s;
}

static u8 * format_nsh_action (u8 * s, va_list * args)
{
  u32 nsh_action = va_arg (*args, u32);

  switch (nsh_action)
    {
    case NSH_ACTION_SWAP:
      return format (s, "swap");
    case NSH_ACTION_PUSH:
      return format (s, "push");
    case NSH_ACTION_POP:
      return format (s, "pop");
    default:
      return format (s, "unknown %d", nsh_action);
    }
  return s;
}

u8 * format_nsh_map (u8 * s, va_list * args)
{
  nsh_map_t * map = va_arg (*args, nsh_map_t *);

  s = format (s, "nsh entry nsp: %d nsi: %d ",
              (map->nsp_nsi>>NSH_NSP_SHIFT) & NSH_NSP_MASK,
              map->nsp_nsi & NSH_NSI_MASK);
  s = format (s, "maps to nsp: %d nsi: %d ",
              (map->mapped_nsp_nsi>>NSH_NSP_SHIFT) & NSH_NSP_MASK,
              map->mapped_nsp_nsi & NSH_NSI_MASK);

  s = format (s, " nsh_action %U\n", format_nsh_action, map->nsh_action);

  switch (map->next_node)
    {
    case NSH_INPUT_NEXT_ENCAP_GRE:
      {
	s = format (s, "encapped by GRE intf: %d", map->sw_if_index);
	break;
      }
    case NSH_INPUT_NEXT_ENCAP_VXLANGPE:
      {
	s = format (s, "encapped by VXLAN GPE intf: %d", map->sw_if_index);
	break;
      }
    default:
      s = format (s, "other: %v idx:%d", vlib_get_next_node(vlib_get_main(),
                                                        nsh_input_node.index, map->next_node)->name,
                  map->sw_if_index);
    }

  return s;
}

u8 * format_nsh_header_with_length (u8 * s, va_list * args)
{
  nsh_header_t * h = va_arg (*args, nsh_header_t *);
  u32 max_header_bytes = va_arg (*args, u32);
  u32 tmp, header_bytes;

  header_bytes = sizeof (h[0]);
  if (max_header_bytes != 0 && header_bytes > max_header_bytes)
    return format (s, "nsh header truncated");

  tmp = clib_net_to_host_u32 (h->nsp_nsi);
  s = format (s, "  nsp %d nsi %d ",
              (tmp>>NSH_NSP_SHIFT) & NSH_NSP_MASK,
              tmp & NSH_NSI_MASK);

  s = format (s, "c1 %u c2 %u c3 %u c4 %u",
              clib_net_to_host_u32 (h->c1),
              clib_net_to_host_u32 (h->c2),
              clib_net_to_host_u32 (h->c3),
              clib_net_to_host_u32 (h->c4));

  s = format (s, "ver %d ", h->ver_o_c>>6);

  if (h->ver_o_c & NSH_O_BIT)
    s = format (s, "O-set ");

  if (h->ver_o_c & NSH_C_BIT)
    s = format (s, "C-set ");

  s = format (s, "len %d (%d bytes) md_type %d next_protocol %d\n",
              h->length, h->length * 4, h->md_type, h->next_protocol);
  return s;
}

u8 * format_nsh_input_map_trace (u8 * s, va_list * args)
{
  CLIB_UNUSED (vlib_main_t * vm) = va_arg (*args, vlib_main_t *);
  CLIB_UNUSED (vlib_node_t * node) = va_arg (*args, vlib_node_t *);
  nsh_input_trace_t * t
    = va_arg (*args, nsh_input_trace_t *);

  s = format (s, "\n  %U", format_nsh_header, &t->nsh_header,
              (u32) sizeof (t->nsh_header) );

  return s;
}

/**
 * Action function to add or del an nsh map.
 * Shared by both CLI and binary API
 **/

int nsh_add_del_map (nsh_add_del_map_args_t *a, u32 * map_indexp)
{
  nsh_main_t * nm = &nsh_main;
  nsh_map_t *map = 0;
  u32 key, *key_copy;
  uword * entry;
  hash_pair_t *hp;
  u32 map_index = ~0;

  key = a->map.nsp_nsi;

  entry = hash_get_mem (nm->nsh_mapping_by_key, &key);

  if (a->is_add)
    {
      /* adding an entry, must not already exist */
      if (entry)
        return -1; //TODO API_ERROR_INVALID_VALUE;

      pool_get_aligned (nm->nsh_mappings, map, CLIB_CACHE_LINE_BYTES);
      memset (map, 0, sizeof (*map));

      /* copy from arg structure */
      map->nsp_nsi = a->map.nsp_nsi;
      map->mapped_nsp_nsi = a->map.mapped_nsp_nsi;
      map->nsh_action = a->map.nsh_action;
      map->sw_if_index = a->map.sw_if_index;
      map->next_node = a->map.next_node;
      map->encap_header[0] = a->map.encap_header[0];
      map->encap_header[1] = a->map.encap_header[1];
      map->encap_length = a->map.encap_length;

      key_copy = clib_mem_alloc (sizeof (*key_copy));
      clib_memcpy (key_copy, &key, sizeof (*key_copy));

      hash_set_mem (nm->nsh_mapping_by_key, key_copy,
                    map - nm->nsh_mappings);
      map_index = map - nm->nsh_mappings;
    }
  else
    {
      if (!entry)
	return -2 ; //TODO API_ERROR_NO_SUCH_ENTRY;

      map = pool_elt_at_index (nm->nsh_mappings, entry[0]);
      hp = hash_get_pair (nm->nsh_mapping_by_key, &key);
      key_copy = (void *)(hp->key);
      hash_unset_mem (nm->nsh_mapping_by_key, &key);
      clib_mem_free (key_copy);

      pool_put (nm->nsh_mappings, map);
    }

  if (map_indexp)
      *map_indexp = map_index;

  return 0;
}

/**
 * CLI command for NSH map
 */

static uword unformat_nsh_action (unformat_input_t * input, va_list * args)
{
  u32 * result = va_arg (*args, u32 *);
  u32 tmp;

  if (unformat (input, "swap"))
    *result = NSH_ACTION_SWAP;
  if (unformat (input, "push"))
    *result = NSH_ACTION_PUSH;
  if (unformat (input, "pop"))
    *result = NSH_ACTION_POP;
  else if (unformat (input, "%d", &tmp))
    *result = tmp;
  else
    return 0;
  return 1;
}

static clib_error_t *
nsh_add_del_map_command_fn (vlib_main_t * vm,
			    unformat_input_t * input,
			    vlib_cli_command_t * cmd)
{
  unformat_input_t _line_input, * line_input = &_line_input;
  u8 is_add = 1;
  u32 nsp, nsi, mapped_nsp, mapped_nsi, nsh_action;
  int nsp_set = 0, nsi_set = 0, mapped_nsp_set = 0, mapped_nsi_set = 0;
  int nsh_action_set = 0;
  u32 next_node = ~0;
  u32 sw_if_index = ~0; // temporary requirement to get this moved over to NSHSFC
  nsh_add_del_map_args_t _a, * a = &_a;
  u32 map_index;
  u8 *node_name = 0;
  int rv;
  u8 *header_data = 0;

  /* Get a line of input. */
  if (! unformat_user (input, unformat_line_input, line_input))
    return 0;

  while (unformat_check_input (line_input) != UNFORMAT_END_OF_INPUT) {
    if (unformat (line_input, "del"))
      is_add = 0;
    else if (unformat (line_input, "nsp %d", &nsp))
      nsp_set = 1;
    else if (unformat (line_input, "nsi %d", &nsi))
      nsi_set = 1;
    else if (unformat (line_input, "mapped-nsp %d", &mapped_nsp))
      mapped_nsp_set = 1;
    else if (unformat (line_input, "mapped-nsi %d", &mapped_nsi))
      mapped_nsi_set = 1;
    else if (unformat (line_input, "nsh_action %U", unformat_nsh_action,
                       &nsh_action))
      nsh_action_set = 1;
    else if (unformat (line_input, "encap-gre-intf %d", &sw_if_index))
      next_node = NSH_INPUT_NEXT_ENCAP_GRE;
    else if (unformat (line_input, "encap-vxlan-gpe-intf %d", &sw_if_index))
      next_node = NSH_INPUT_NEXT_ENCAP_VXLANGPE;
    else if (unformat (line_input, "encap-none"))
      next_node = NSH_INPUT_NEXT_DROP; // Once moved to NSHSFC see nsh.h:foreach_nsh_input_next to handle this case
    else if (unformat (line_input, "encap-other %s %d", &node_name, &sw_if_index))
      {
	vlib_node_t * input_node = vlib_get_node(vm, nsh_input_node.index);
	vlib_node_t * next_node_n = vlib_get_node_by_name(vm, node_name);
	if (!next_node_n)
	  return clib_error_return (0, "no such node: %s", node_name);

	next_node = ~0;
	int i;
	for (i=0; i<vec_len (input_node->next_nodes); i++)
	  if (input_node->next_nodes[i] == next_node_n->index)
	    {
	      next_node = i;
	      break;
	    }

	if (next_node == ~0)
	  return clib_error_return (0, "invalid next node '%s' for nsh-input",
				    node_name);
	vec_free(node_name);
      }
    else if (unformat (line_input, "encap-ethernet %d %U", &sw_if_index, unformat_hex_string, &header_data))
      {
	vnet_main_t *vnm = vnet_get_main();
	vnet_hw_interface_t *h = vnet_get_sup_hw_interface(vnm, sw_if_index);
	u32 output_node_index = h->output_node_index;
	next_node = vlib_node_add_next(vm, nsh_input_node.index, output_node_index);
      }
    else
      return clib_error_return (0, "parse error: '%U'",
                                format_unformat_error, line_input);
  }

  unformat_free (line_input);

  if (nsp_set == 0 || nsi_set == 0)
    return clib_error_return (0, "nsp nsi pair required. Key: for NSH entry");

  if (mapped_nsp_set == 0 || mapped_nsi_set == 0)
    return clib_error_return (0, "mapped-nsp mapped-nsi pair required. Key: for NSH entry");

  if (nsh_action_set == 0 )
    return clib_error_return (0, "nsh_action required: swap|push|pop.");

  if (next_node == ~0)
    return clib_error_return (0, "must specific action: [encap-gre-intf <nn> | encap-vxlan-gpe-intf <nn> | "
			      "encap-none | encap-other <name> <sw_if_index>] | encap-ethernet <sw-if-index> <ethernet-header>");

  memset (a, 0, sizeof (*a));

  /* set args structure */
  a->is_add = is_add;
  a->map.nsp_nsi = (nsp<< NSH_NSP_SHIFT) | nsi;
  a->map.mapped_nsp_nsi = (mapped_nsp<< NSH_NSP_SHIFT) | mapped_nsi;
  a->map.nsh_action = nsh_action;
  a->map.sw_if_index = sw_if_index;
  a->map.next_node = next_node;
  a->map.encap_length = vec_len(header_data);
  if (vec_len(header_data))
    memcpy(a->map.encap_header, header_data, vec_len(header_data));

  rv = nsh_add_del_map (a, &map_index);

  switch(rv)
    {
    case 0:
      break;
    case -1: //TODO API_ERROR_INVALID_VALUE:
      return clib_error_return (0, "mapping already exists. Remove it first.");

    case -2: // TODO API_ERROR_NO_SUCH_ENTRY:
      return clib_error_return (0, "mapping does not exist.");

    default:
      return clib_error_return
        (0, "nsh_add_del_map returned %d", rv);
    }
  return 0;
}

VLIB_CLI_COMMAND (create_nsh_map_command, static) = {
  .path = "create nsh map",
  .short_help =
  "create nsh map nsp <nn> nsi <nn> [del] mapped-nsp <nn> mapped-nsi <nn> nsh_action [swap|push|pop] "
  "[encap-gre-intf <nn> | encap-vxlan-gpe-intf <nn> | encap-none | encap-other <name> <sw_if_index>]\n",
  .function = nsh_add_del_map_command_fn,
};

/** API message handler */
static void vl_api_nsh_add_del_map_t_handler
(vl_api_nsh_add_del_map_t * mp)
{
  vl_api_nsh_add_del_map_reply_t * rmp;
  nsh_main_t * nm = &nsh_main;
  int rv;
  nsh_add_del_map_args_t _a, *a = &_a;
  u32 map_index = ~0;

  a->is_add = mp->is_add;
  a->map.nsp_nsi = ntohl(mp->nsp_nsi);
  a->map.mapped_nsp_nsi = ntohl(mp->mapped_nsp_nsi);
  a->map.nsh_action = ntohl(mp->nsh_action);
  a->map.sw_if_index = ntohl(mp->sw_if_index);
  a->map.next_node = ntohl(mp->next_node);

  rv = nsh_add_del_map (a, &map_index);

  REPLY_MACRO2(VL_API_NSH_ADD_DEL_MAP_REPLY,
  ({
    rmp->map_index = htonl (map_index);
  }));
}

/**
 * CLI command for showing the mapping between NSH entries
 */
static clib_error_t *
show_nsh_map_command_fn (vlib_main_t * vm,
			 unformat_input_t * input,
			 vlib_cli_command_t * cmd)
{
  nsh_main_t * nm = &nsh_main;
  nsh_map_t * map;

  if (pool_elts (nm->nsh_mappings) == 0)
    vlib_cli_output (vm, "No nsh maps configured.");

  pool_foreach (map, nm->nsh_mappings,
		({
		  vlib_cli_output (vm, "%U", format_nsh_map, map);
		}));

  return 0;
}

VLIB_CLI_COMMAND (show_nsh_map_command, static) = {
  .path = "show nsh map",
  .function = show_nsh_map_command_fn,
};

/**
 * Action function for adding an NSH entry
 */

int nsh_add_del_entry (nsh_add_del_entry_args_t *a, u32 * entry_indexp)
{
  nsh_main_t * nm = &nsh_main;
  nsh_header_t *hdr = 0;
  u32 key, *key_copy;
  uword * entry;
  hash_pair_t *hp;
  u32 entry_index = ~0;

  key = a->nsh.nsp_nsi;

  entry = hash_get_mem (nm->nsh_entry_by_key, &key);

  if (a->is_add)
    {
      /* adding an entry, must not already exist */
      if (entry)
        return -1; // TODO VNET_API_ERROR_INVALID_VALUE;

      pool_get_aligned (nm->nsh_entries, hdr, CLIB_CACHE_LINE_BYTES);
      memset (hdr, 0, sizeof (*hdr));

      /* copy from arg structure */
#define _(x) hdr->x = a->nsh.x;
      foreach_copy_nshhdr_field;
#undef _

      key_copy = clib_mem_alloc (sizeof (*key_copy));
      clib_memcpy (key_copy, &key, sizeof (*key_copy));

      hash_set_mem (nm->nsh_entry_by_key, key_copy,
                    hdr - nm->nsh_entries);
      entry_index = hdr - nm->nsh_entries;
    }
  else
    {
      if (!entry)
	return -2; //TODO API_ERROR_NO_SUCH_ENTRY;

      hdr = pool_elt_at_index (nm->nsh_entries, entry[0]);
      hp = hash_get_pair (nm->nsh_entry_by_key, &key);
      key_copy = (void *)(hp->key);
      hash_unset_mem (nm->nsh_entry_by_key, &key);
      clib_mem_free (key_copy);

      pool_put (nm->nsh_entries, hdr);
    }

  if (entry_indexp)
      *entry_indexp = entry_index;

  return 0;
}


/**
 * CLI command for adding NSH entry
 */

static clib_error_t *
nsh_add_del_entry_command_fn (vlib_main_t * vm,
			      unformat_input_t * input,
			      vlib_cli_command_t * cmd)
{
  unformat_input_t _line_input, * line_input = &_line_input;
  u8 is_add = 1;
  u8 ver_o_c = 0;
  u8 length = 0;
  u8 md_type = 0;
  u8 next_protocol = 1; /* default: ip4 */
  u32 nsp;
  u8 nsp_set = 0;
  u32 nsi;
  u8 nsi_set = 0;
  u32 nsp_nsi;
  u32 c1 = 0;
  u32 c2 = 0;
  u32 c3 = 0;
  u32 c4 = 0;
  u32 tmp;
  int rv;
  u32 entry_index;
  nsh_add_del_entry_args_t _a, * a = &_a;

  /* Get a line of input. */
  if (! unformat_user (input, unformat_line_input, line_input))
    return 0;

  while (unformat_check_input (line_input) != UNFORMAT_END_OF_INPUT) {
    if (unformat (line_input, "del"))
      is_add = 0;
    else if (unformat (line_input, "version %d", &tmp))
      ver_o_c |= (tmp & 3) << 6;
    else if (unformat (line_input, "o-bit %d", &tmp))
      ver_o_c |= (tmp & 1) << 5;
    else if (unformat (line_input, "c-bit %d", &tmp))
      ver_o_c |= (tmp & 1) << 4;
    else if (unformat (line_input, "md-type %d", &tmp))
      md_type = tmp;
    else if (unformat(line_input, "next-ip4"))
      next_protocol = 1;
    else if (unformat(line_input, "next-ip6"))
      next_protocol = 2;
    else if (unformat(line_input, "next-ethernet"))
      next_protocol = 3;
    else if (unformat (line_input, "c1 %d", &c1))
      ;
    else if (unformat (line_input, "c2 %d", &c2))
      ;
    else if (unformat (line_input, "c3 %d", &c3))
      ;
    else if (unformat (line_input, "c4 %d", &c4))
      ;
    else if (unformat (line_input, "nsp %d", &nsp))
      nsp_set = 1;
    else if (unformat (line_input, "nsi %d", &nsi))
      nsi_set = 1;
    else
      return clib_error_return (0, "parse error: '%U'",
                                format_unformat_error, line_input);
  }

  unformat_free (line_input);

  if (nsp_set == 0)
    return clib_error_return (0, "nsp not specified");

  if (nsi_set == 0)
    return clib_error_return (0, "nsi not specified");

  if (md_type != 1)
    return clib_error_return (0, "md-type 1 only supported at this time");

  md_type = 1;
  length = 6;

  nsp_nsi = (nsp<<8) | nsi;

  memset (a, 0, sizeof (*a));

  a->is_add = is_add;

#define _(x) a->nsh.x = x;
  foreach_copy_nshhdr_field;
#undef _

  rv = nsh_add_del_entry (a, &entry_index);

  switch(rv)
    {
    case 0:
      break;
    default:
      return clib_error_return
        (0, "nsh_add_del_entry returned %d", rv);
    }

  return 0;
}

VLIB_CLI_COMMAND (create_nsh_entry_command, static) = {
  .path = "create nsh entry",
  .short_help =
  "create nsh entry {nsp <nn> nsi <nn>} c1 <nn> c2 <nn> c3 <nn> c4 <nn>"
  " [md-type <nn>] [tlv <xx>] [del]\n",
  .function = nsh_add_del_entry_command_fn,
};

/** API message handler */
static void vl_api_nsh_add_del_entry_t_handler
(vl_api_nsh_add_del_entry_t * mp)
{
  vl_api_nsh_add_del_entry_reply_t * rmp;
  nsh_main_t * nm = &nsh_main;
  int rv;
  nsh_add_del_entry_args_t _a, *a = &_a;
  u32 entry_index = ~0;

  a->is_add = mp->is_add;
  a->nsh.ver_o_c = mp->ver_o_c;
  a->nsh.length = mp->length;
  a->nsh.md_type = mp->md_type;
  a->nsh.next_protocol = mp->next_protocol;
  a->nsh.nsp_nsi = ntohl(mp->nsp_nsi);
  a->nsh.c1 = ntohl(mp->c1);
  a->nsh.c2 = ntohl(mp->c2);
  a->nsh.c3 = ntohl(mp->c3);
  a->nsh.c4 = ntohl(mp->c4);

  rv = nsh_add_del_entry (a, &entry_index);

  REPLY_MACRO2(VL_API_NSH_ADD_DEL_ENTRY_REPLY,
  ({
    rmp->entry_index = htonl (entry_index);
  }));
}

static void send_nsh_entry_details
(nsh_header_t * t, unix_shared_memory_queue_t * q, u32 context)
{
    vl_api_nsh_entry_details_t * rmp;
    nsh_main_t * nm = &nsh_main;

    rmp = vl_msg_api_alloc (sizeof (*rmp));
    memset (rmp, 0, sizeof (*rmp));

    rmp->_vl_msg_id = ntohs((VL_API_NSH_ENTRY_DETAILS)+nm->msg_id_base);
    rmp->ver_o_c = t->ver_o_c;
    rmp->length = t->length;
    rmp->md_type = t->md_type;
    rmp->next_protocol = t->next_protocol;
    rmp->nsp_nsi = htonl(t->nsp_nsi);
    rmp->c1 = htonl(t->c1);
    rmp->c2 = htonl(t->c2);
    rmp->c3 = htonl(t->c3);
    rmp->c4 = htonl(t->c4);

    rmp->context = context;

    vl_msg_api_send_shmem (q, (u8 *)&rmp);
}

static void vl_api_nsh_entry_dump_t_handler
(vl_api_nsh_entry_dump_t * mp)
{
    unix_shared_memory_queue_t * q;
    nsh_main_t * nm = &nsh_main;
    nsh_header_t * t;
    u32 entry_index;

    q = vl_api_client_index_to_input_queue (mp->client_index);
    if (q == 0) {
        return;
    }

    entry_index = ntohl (mp->entry_index);

    if (~0 == entry_index)
      {
	pool_foreach (t, nm->nsh_entries,
	({
	    send_nsh_entry_details(t, q, mp->context);
	}));
      }
    else
      {
        if (entry_index >= vec_len (nm->nsh_entries))
  	{
  	  return;
  	}
        t = &nm->nsh_entries[entry_index];
        send_nsh_entry_details(t, q, mp->context);
      }
}

static void send_nsh_map_details
(nsh_map_t * t, unix_shared_memory_queue_t * q, u32 context)
{
    vl_api_nsh_map_details_t * rmp;
    nsh_main_t * nm = &nsh_main;

    rmp = vl_msg_api_alloc (sizeof (*rmp));
    memset (rmp, 0, sizeof (*rmp));

    rmp->_vl_msg_id = ntohs((VL_API_NSH_MAP_DETAILS)+nm->msg_id_base);
    rmp->nsp_nsi = htonl(t->nsp_nsi);
    rmp->mapped_nsp_nsi = htonl(t->mapped_nsp_nsi);
    rmp->nsh_action = htonl(t->nsh_action);
    rmp->sw_if_index = htonl(t->sw_if_index);
    rmp->next_node = htonl(t->next_node);

    rmp->context = context;

    vl_msg_api_send_shmem (q, (u8 *)&rmp);
}

static void vl_api_nsh_map_dump_t_handler
(vl_api_nsh_map_dump_t * mp)
{
    unix_shared_memory_queue_t * q;
    nsh_main_t * nm = &nsh_main;
    nsh_map_t * t;
    u32 map_index;

    q = vl_api_client_index_to_input_queue (mp->client_index);
    if (q == 0) {
        return;
    }

    map_index = ntohl (mp->map_index);

    if (~0 == map_index)
      {
	pool_foreach (t, nm->nsh_mappings,
	({
	    send_nsh_map_details(t, q, mp->context);
	}));
      }
    else
      {
        if (map_index >= vec_len (nm->nsh_mappings))
  	{
  	  return;
  	}
        t = &nm->nsh_mappings[map_index];
        send_nsh_map_details(t, q, mp->context);
      }
}

static clib_error_t *
show_nsh_entry_command_fn (vlib_main_t * vm,
			   unformat_input_t * input,
			   vlib_cli_command_t * cmd)
{
  nsh_main_t * nm = &nsh_main;
  nsh_header_t * hdr;

  if (pool_elts (nm->nsh_entries) == 0)
    vlib_cli_output (vm, "No nsh entries configured.");

  pool_foreach (hdr, nm->nsh_entries,
		({
		  vlib_cli_output (vm, "%U", format_nsh_header, hdr);
		}));

  return 0;
}

VLIB_CLI_COMMAND (show_nsh_entry_command, static) = {
  .path = "show nsh entry",
  .function = show_nsh_entry_command_fn,
};


/* Set up the API message handling tables */
static clib_error_t *
nsh_plugin_api_hookup (vlib_main_t *vm)
{
  nsh_main_t * nm = &nsh_main;
#define _(N,n)                                                  \
  vl_msg_api_set_handlers((VL_API_##N + nm->msg_id_base),	\
			  #n,					\
			  vl_api_##n##_t_handler,		\
			  vl_noop_handler,			\
			  vl_api_##n##_t_endian,		\
			  vl_api_##n##_t_print,			\
			  sizeof(vl_api_##n##_t), 1);
  foreach_nsh_plugin_api_msg;
#undef _

  return 0;
}




static uword
nsh_input_map (vlib_main_t * vm,
               vlib_node_runtime_t * node,
               vlib_frame_t * from_frame)
{
  u32 n_left_from, next_index, *from, *to_next;
  nsh_main_t * nm = &nsh_main;

  from = vlib_frame_vector_args(from_frame);
  n_left_from = from_frame->n_vectors;

  next_index = node->cached_next_index;

  while (n_left_from > 0)
    {
      u32 n_left_to_next;

      vlib_get_next_frame(vm, node, next_index, to_next, n_left_to_next);
#if 0
      while (n_left_from >= 4 && n_left_to_next >= 2)
	{
	  u32 bi0, bi1;
	  vlib_buffer_t * b0, *b1;
	  u32 next0 = NSH_INPUT_NEXT_DROP, next1 = NSH_INPUT_NEXT_DROP;
	  uword * entry0, *entry1;
	  nsh_header_t * hdr0 = 0, *hdr1 = 0;
	  u32 nsp_nsi0, nsp_nsi1;
	  u32 error0, error1;
	  nsh_map_t * map0 = 0, *map1 = 0;

	  /* Prefetch next iteration. */
	  {
	    vlib_buffer_t * p2, *p3;

	    p2 = vlib_get_buffer(vm, from[2]);
	    p3 = vlib_get_buffer(vm, from[3]);

	    vlib_prefetch_buffer_header(p2, LOAD);
	    vlib_prefetch_buffer_header(p3, LOAD);

	    CLIB_PREFETCH(p2->data, 2*CLIB_CACHE_LINE_BYTES, LOAD);
	    CLIB_PREFETCH(p3->data, 2*CLIB_CACHE_LINE_BYTES, LOAD);
	  }

	  bi0 = from[0];
	  bi1 = from[1];
	  to_next[0] = bi0;
	  to_next[1] = bi1;
	  from += 2;
	  to_next += 2;
	  n_left_from -= 2;
	  n_left_to_next -= 2;

	  error0 = 0;
	  error1 = 0;

	  b0 = vlib_get_buffer(vm, bi0);
	  hdr0 = vlib_buffer_get_current(b0);
	  nsp_nsi0 = clib_net_to_host_u32(hdr0->nsp_nsi);
	  entry0 = hash_get_mem(nm->nsh_mapping_by_key, &nsp_nsi0);

	  b1 = vlib_get_buffer(vm, bi1);
	  hdr1 = vlib_buffer_get_current(b1);
	  nsp_nsi1 = clib_net_to_host_u32(hdr1->nsp_nsi);
	  entry1 = hash_get_mem(nm->nsh_mapping_by_key, &nsp_nsi1);

	  if (PREDICT_FALSE(entry0 == 0))
	    {
	      error0 = NSH_INPUT_ERROR_NO_MAPPING;
	      goto trace0;
	    }

	  if (PREDICT_FALSE(entry1 == 0))
	    {
	      error1 = NSH_INPUT_ERROR_NO_MAPPING;
	      goto trace1;
	    }

	  /* Entry should point to a mapping ...*/
	  map0 = pool_elt_at_index(nm->nsh_mappings, entry0[0]);
	  map1 = pool_elt_at_index(nm->nsh_mappings, entry1[0]);

	  if (PREDICT_FALSE(map0 == 0))
	    {
	      error0 = NSH_INPUT_ERROR_NO_MAPPING;
	      goto trace0;
	    }

	  if (PREDICT_FALSE(map1 == 0))
	    {
	      error1 = NSH_INPUT_ERROR_NO_MAPPING;
	      goto trace1;
	    }

	  entry0 = hash_get_mem(nm->nsh_entry_by_key, &map0->mapped_nsp_nsi);
	  entry1 = hash_get_mem(nm->nsh_entry_by_key, &map1->mapped_nsp_nsi);

	  if (PREDICT_FALSE(entry0 == 0))
	    {
	      error0 = NSH_INPUT_ERROR_NO_ENTRY;
	      goto trace0;
	    }
	  if (PREDICT_FALSE(entry1 == 0))
	    {
	      error1 = NSH_INPUT_ERROR_NO_ENTRY;
	      goto trace1;
	    }

	  hdr0 = pool_elt_at_index(nm->nsh_entries, entry0[0]);
	  hdr1 = pool_elt_at_index(nm->nsh_entries, entry1[0]);

	  /* set up things for next node to transmit ie which node to handle it and where */
	  next0 = map0->next_node;
	  next1 = map1->next_node;
	  vnet_buffer(b0)->sw_if_index[VLIB_TX] = map0->sw_if_index;
	  vnet_buffer(b1)->sw_if_index[VLIB_TX] = map1->sw_if_index;

	trace0: b0->error = error0 ? node->errors[error0] : 0;

	  if (PREDICT_FALSE(b0->flags & VLIB_BUFFER_IS_TRACED))
	    {
	      nsh_input_trace_t *tr = vlib_add_trace(vm, node, b0, sizeof(*tr));
	      tr->nsh_header = *hdr0;
	    }

	trace1: b1->error = error1 ? node->errors[error1] : 0;

	  if (PREDICT_FALSE(b1->flags & VLIB_BUFFER_IS_TRACED))
	    {
	      nsh_input_trace_t *tr = vlib_add_trace(vm, node, b1, sizeof(*tr));
	      tr->nsh_header = *hdr1;
	    }

	  vlib_validate_buffer_enqueue_x2(vm, node, next_index, to_next,
					  n_left_to_next, bi0, bi1, next0, next1);

	}
#endif
      while (n_left_from > 0 && n_left_to_next > 0)
	{
	  u32 bi0;
	  vlib_buffer_t * b0;
	  u32 next0 = NSH_INPUT_NEXT_DROP;
	  uword * entry0;
	  nsh_header_t * hdr0 = 0;
	  u32 nsp_nsi0;
	  u32 error0;
	  nsh_map_t * map0 = 0;

	  bi0 = from[0];
	  to_next[0] = bi0;
	  from += 1;
	  to_next += 1;
	  n_left_from -= 1;
	  n_left_to_next -= 1;
	  error0 = 0;

	  b0 = vlib_get_buffer(vm, bi0);
	  hdr0 = vlib_buffer_get_current(b0);
	  nsp_nsi0 = clib_net_to_host_u32(hdr0->nsp_nsi);
	  entry0 = hash_get_mem(nm->nsh_mapping_by_key, &nsp_nsi0);

	  if (PREDICT_FALSE(entry0 == 0))
	    {
	      error0 = NSH_INPUT_ERROR_NO_MAPPING;
	      goto trace00;
	    }

	  /* Entry should point to a mapping ...*/
	  map0 = pool_elt_at_index(nm->nsh_mappings, entry0[0]);

	  if (PREDICT_FALSE(map0 == 0))
	    {
	      error0 = NSH_INPUT_ERROR_NO_MAPPING;
	      goto trace00;
	    }

	  entry0 = hash_get_mem(nm->nsh_entry_by_key, &map0->mapped_nsp_nsi);

	  if (PREDICT_FALSE(entry0 == 0))
	    {
	      error0 = NSH_INPUT_ERROR_NO_ENTRY;
	      goto trace00;
	    }

	  hdr0 = pool_elt_at_index(nm->nsh_entries, entry0[0]);

	  /* set up things for next node to transmit ie which node to handle it and where */
	  next0 = map0->next_node;
	  vnet_buffer(b0)->sw_if_index[VLIB_TX] = map0->sw_if_index;

	  vlib_buffer_advance(b0, -map0->encap_length);
	  clib_memcpy(vlib_buffer_get_current(b0), map0->encap_header, map0->encap_length);

	trace00: b0->error = error0 ? node->errors[error0] : 0;

	  if (PREDICT_FALSE(b0->flags & VLIB_BUFFER_IS_TRACED))
	    {
	      nsh_input_trace_t *tr = vlib_add_trace(vm, node, b0, sizeof(*tr));
	      tr->nsh_header = *hdr0;
	      vlib_trace_next_frame(vm, node, next0);
	    }

	  vlib_validate_buffer_enqueue_x1(vm, node, next_index, to_next,
					  n_left_to_next, bi0, next0);
	}

      vlib_put_next_frame(vm, node, next_index, n_left_to_next);

    }

  return from_frame->n_vectors;
}


static char * nsh_input_error_strings[] = {
#define _(sym,string) string,
  foreach_nsh_input_error
#undef _
};

VLIB_REGISTER_NODE (nsh_input_node) = {
  .function = nsh_input_map,
  .name = "nsh-input",
  .vector_size = sizeof (u32),
  .format_trace = format_nsh_input_map_trace,
  .format_buffer = format_nsh_header_with_length,
  .type = VLIB_NODE_TYPE_INTERNAL,

  .n_errors = ARRAY_LEN(nsh_input_error_strings),
  .error_strings = nsh_input_error_strings,

  .n_next_nodes = NSH_INPUT_N_NEXT,

  .next_nodes = {
#define _(s,n) [NSH_INPUT_NEXT_##s] = n,
    foreach_nsh_input_next
#undef _
  },
};

clib_error_t *nsh_init (vlib_main_t *vm)
{
  nsh_main_t *nm = &nsh_main;
  clib_error_t * error = 0;
  vlib_node_t * vxlan4_gpe_input_node = 0;
  vlib_node_t * vxlan6_gpe_input_node = 0;
  vlib_node_t * gre_input_node = 0;
  u8 * name;

  /* Init the main structures from VPP */
  nm->vlib_main = vm;
  nm->vnet_main = vnet_get_main();

  /* Various state maintenance mappings */
  nm->nsh_mapping_by_key
    = hash_create_mem (0, sizeof(u32), sizeof (uword));

  nm->nsh_mapping_by_mapped_key
    = hash_create_mem (0, sizeof(u32), sizeof (uword));

  nm->nsh_entry_by_key
    = hash_create_mem (0, sizeof(u32), sizeof (uword));

  name = format (0, "nsh_%08x%c", api_version, 0);

  /* Set up the API */
  nm->msg_id_base = vl_msg_api_get_msg_ids
    ((char *) name, VL_MSG_FIRST_AVAILABLE);

  error = nsh_plugin_api_hookup (vm);

  /* Add dispositions to nodes that feed nsh-input */
  vxlan4_gpe_input_node = vlib_get_node_by_name (vm, (u8 *)"vxlan4-gpe-input");
  ASSERT(vxlan4_gpe_input_node);
  //alagalah - validate we don't really need to use the node value
  vlib_node_add_next (vm, vxlan4_gpe_input_node->index, nsh_input_node.index);

  vxlan6_gpe_input_node = vlib_get_node_by_name (vm, (u8 *)"vxlan6-gpe-input");
  ASSERT(vxlan6_gpe_input_node);
  vlib_node_add_next (vm, vxlan6_gpe_input_node->index, nsh_input_node.index);

  gre_input_node = vlib_get_node_by_name (vm, (u8 *)"gre-input");
  ASSERT(gre_input_node);
  vlib_node_add_next (vm, gre_input_node->index, nsh_input_node.index);

  ethernet_register_input_type(vm, ETHERNET_TYPE_VPATH_3, nsh_input_node.index);

  vec_free(name);

  return error;
}

VLIB_INIT_FUNCTION(nsh_init);
