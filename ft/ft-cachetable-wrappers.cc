/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil -*- */
// vim: ft=cpp:expandtab:ts=8:sw=4:softtabstop=4:
#ident "$Id$"
#ident "Copyright (c) 2007-2012 Tokutek Inc.  All rights reserved."
#ident "The technology is licensed by the Massachusetts Institute of Technology, Rutgers State University of New Jersey, and the Research Foundation of State University of New York at Stony Brook under United States of America Serial No. 11/760379 and to the patents and/or patent applications resulting from it."

#include <ft-cachetable-wrappers.h>

#include <fttypes.h>
#include <ft-flusher.h>
#include <ft-internal.h>
#include "ft.h"

static void
ftnode_get_key_and_fullhash(
    BLOCKNUM* cachekey,
    uint32_t* fullhash,
    void* extra)
{
    FT h = (FT) extra;
    BLOCKNUM name;
    toku_allocate_blocknum(h->blocktable, &name, h);
    *cachekey = name;
    *fullhash = toku_cachetable_hash(h->cf, name);
}

void
cachetable_put_empty_node_with_dep_nodes(
    FT h,
    uint32_t num_dependent_nodes,
    FTNODE* dependent_nodes,
    BLOCKNUM* name, //output
    uint32_t* fullhash, //output
    FTNODE* result)
{
    FTNODE XMALLOC(new_node);
    CACHEFILE dependent_cf[num_dependent_nodes];
    BLOCKNUM dependent_keys[num_dependent_nodes];
    uint32_t dependent_fullhash[num_dependent_nodes];
    enum cachetable_dirty dependent_dirty_bits[num_dependent_nodes];
    for (uint32_t i = 0; i < num_dependent_nodes; i++) {
        dependent_cf[i] = h->cf;
        dependent_keys[i] = dependent_nodes[i]->thisnodename;
        dependent_fullhash[i] = toku_cachetable_hash(h->cf, dependent_nodes[i]->thisnodename);
        dependent_dirty_bits[i] = (enum cachetable_dirty) dependent_nodes[i]->dirty;
    }

    int r = toku_cachetable_put_with_dep_pairs(
        h->cf,
        ftnode_get_key_and_fullhash,
        new_node,
        make_pair_attr(sizeof(FTNODE)),
        get_write_callbacks_for_node(h),
        h,
        num_dependent_nodes,
        dependent_cf,
        dependent_keys,
        dependent_fullhash,
        dependent_dirty_bits,
        name,
        fullhash,
        toku_node_save_ct_pair);
    assert_zero(r);
    *result = new_node;
}

void
create_new_ftnode_with_dep_nodes(
    FT ft,
    FTNODE *result,
    int height,
    int n_children,
    uint32_t num_dependent_nodes,
    FTNODE* dependent_nodes)
{
    uint32_t fullhash = 0;
    BLOCKNUM name;

    cachetable_put_empty_node_with_dep_nodes(
        ft,
        num_dependent_nodes,
        dependent_nodes,
        &name,
        &fullhash,
        result);

    assert(ft->h->nodesize > 0);
    assert(ft->h->basementnodesize > 0);
    if (height == 0) {
        assert(n_children > 0);
    }

    toku_initialize_empty_ftnode(
        *result,
        name,
        height,
        n_children,
        ft->h->layout_version,
        ft->h->nodesize,
        ft->h->flags);

    assert((*result)->nodesize > 0);
    (*result)->fullhash = fullhash;
}

void
toku_create_new_ftnode (
    FT_HANDLE t,
    FTNODE *result,
    int height,
    int n_children)
{
    return create_new_ftnode_with_dep_nodes(
        t->ft,
        result,
        height,
        n_children,
        0,
        NULL);
}

int
toku_pin_ftnode_batched(
    FT_HANDLE brt,
    BLOCKNUM blocknum,
    uint32_t fullhash,
    UNLOCKERS unlockers,
    ANCESTORS ancestors,
    const PIVOT_BOUNDS bounds,
    FTNODE_FETCH_EXTRA bfe,
    pair_lock_type lock_type,
    bool apply_ancestor_messages, // this bool is probably temporary, for #3972, once we know how range query estimates work, will revisit this
    bool end_batch_on_success,
    FTNODE *node_p,
    bool* msgs_applied)
{
    void *node_v;
    *msgs_applied = false;
    int r = toku_cachetable_get_and_pin_nonblocking_batched(
            brt->ft->cf,
            blocknum,
            fullhash,
            &node_v,
            NULL,
            get_write_callbacks_for_node(brt->ft),
            toku_ftnode_fetch_callback,
            toku_ftnode_pf_req_callback,
            toku_ftnode_pf_callback,
            lock_type,
            bfe, //read_extraargs
            unlockers);
    if (r==0) {
        FTNODE node = (FTNODE) node_v;
        if (end_batch_on_success) {
            toku_cachetable_end_batched_pin(brt->ft->cf);
        }
        if (apply_ancestor_messages && node->height == 0) {
            toku_apply_ancestors_messages_to_node(brt, node, ancestors, bounds, msgs_applied);
        }
        if ((lock_type != PL_READ) && node->height > 0) {
            toku_move_ftnode_messages_to_stale(brt->ft, node);
        }
        *node_p = node;
        // printf("%*sPin %ld\n", 8-node->height, "", blocknum.b);
    } else {
        assert(r==TOKUDB_TRY_AGAIN); // Any other error and we should bomb out ASAP.
        // printf("%*sPin %ld try again\n", 8, "", blocknum.b);
    }
    return r;
}

void
toku_pin_ftnode_off_client_thread_and_maybe_move_messages(
    FT h,
    BLOCKNUM blocknum,
    uint32_t fullhash,
    FTNODE_FETCH_EXTRA bfe,
    pair_lock_type lock_type,
    uint32_t num_dependent_nodes,
    FTNODE* dependent_nodes,
    FTNODE *node_p,
    bool move_messages)
{
    toku_cachetable_begin_batched_pin(h->cf);
    toku_pin_ftnode_off_client_thread_batched_and_maybe_move_messages(
        h,
        blocknum,
        fullhash,
        bfe,
        lock_type,
        num_dependent_nodes,
        dependent_nodes,
        node_p,
        move_messages
        );
    toku_cachetable_end_batched_pin(h->cf);
}

void
toku_pin_ftnode_off_client_thread(
    FT h,
    BLOCKNUM blocknum,
    uint32_t fullhash,
    FTNODE_FETCH_EXTRA bfe,
    pair_lock_type lock_type,
    uint32_t num_dependent_nodes,
    FTNODE* dependent_nodes,
    FTNODE *node_p)
{
    toku_pin_ftnode_off_client_thread_and_maybe_move_messages(
            h, blocknum, fullhash, bfe, lock_type, num_dependent_nodes, dependent_nodes, node_p, true);
}

void
toku_pin_ftnode_off_client_thread_batched_and_maybe_move_messages(
    FT h,
    BLOCKNUM blocknum,
    uint32_t fullhash,
    FTNODE_FETCH_EXTRA bfe,
    pair_lock_type lock_type,
    uint32_t num_dependent_nodes,
    FTNODE* dependent_nodes,
    FTNODE *node_p,
    bool move_messages)
{
    void *node_v;
    CACHEFILE dependent_cf[num_dependent_nodes];
    BLOCKNUM dependent_keys[num_dependent_nodes];
    uint32_t dependent_fullhash[num_dependent_nodes];
    enum cachetable_dirty dependent_dirty_bits[num_dependent_nodes];
    for (uint32_t i = 0; i < num_dependent_nodes; i++) {
        dependent_cf[i] = h->cf;
        dependent_keys[i] = dependent_nodes[i]->thisnodename;
        dependent_fullhash[i] = toku_cachetable_hash(h->cf, dependent_nodes[i]->thisnodename);
        dependent_dirty_bits[i] = (enum cachetable_dirty) dependent_nodes[i]->dirty;
    }

    int r = toku_cachetable_get_and_pin_with_dep_pairs_batched(
        h->cf,
        blocknum,
        fullhash,
        &node_v,
        NULL,
        get_write_callbacks_for_node(h),
        toku_ftnode_fetch_callback,
        toku_ftnode_pf_req_callback,
        toku_ftnode_pf_callback,
        lock_type,
        bfe,
        num_dependent_nodes,
        dependent_cf,
        dependent_keys,
        dependent_fullhash,
        dependent_dirty_bits
        );
    assert(r==0);
    FTNODE node = (FTNODE) node_v;
    if ((lock_type != PL_READ) && node->height > 0 && move_messages) {
        toku_move_ftnode_messages_to_stale(h, node);
    }
    *node_p = node;
}

void
toku_pin_ftnode_off_client_thread_batched(
    FT h,
    BLOCKNUM blocknum,
    uint32_t fullhash,
    FTNODE_FETCH_EXTRA bfe,
    pair_lock_type lock_type,
    uint32_t num_dependent_nodes,
    FTNODE* dependent_nodes,
    FTNODE *node_p)
{
    toku_pin_ftnode_off_client_thread_batched_and_maybe_move_messages(
            h, blocknum, fullhash, bfe, lock_type, num_dependent_nodes, dependent_nodes, node_p, true);
}

int toku_maybe_pin_ftnode_clean(FT ft, BLOCKNUM blocknum, uint32_t fullhash, FTNODE *nodep) {
    void *node_v;
    int r = toku_cachetable_maybe_get_and_pin_clean(ft->cf, blocknum, fullhash, &node_v);
    if (r != 0) {
        goto cleanup;
    }
    CAST_FROM_VOIDP(*nodep, node_v);
    if ((*nodep)->height > 0) {
        toku_move_ftnode_messages_to_stale(ft, *nodep);
    }
cleanup:
    return r;
}

void
toku_unpin_ftnode_off_client_thread(FT ft, FTNODE node)
{
    int r = toku_cachetable_unpin(
        ft->cf,
        node->ct_pair,
        (enum cachetable_dirty) node->dirty,
        make_ftnode_pair_attr(node)
        );
    assert(r==0);
}

void
toku_unpin_ftnode(FT ft, FTNODE node)
{
    // printf("%*sUnpin %ld\n", 8-node->height, "", node->thisnodename.b);
    //VERIFY_NODE(brt,node);
    toku_unpin_ftnode_off_client_thread(ft, node);
}

void
toku_unpin_ftnode_read_only(FT_HANDLE brt, FTNODE node)
{
    int r = toku_cachetable_unpin(
        brt->ft->cf,
        node->ct_pair,
        (enum cachetable_dirty) node->dirty,
        make_invalid_pair_attr()
        );
    assert(r==0);
}
