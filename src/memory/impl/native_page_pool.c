/*!The Treasure Box Library
 * 
 * TBox is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 * 
 * TBox is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public License
 * along with TBox; 
 * If not, see <a href="http://www.gnu.org/licenses/"> http://www.gnu.org/licenses/</a>
 * 
 * Copyright (C) 2009 - 2015, ruki All rights reserved.
 *
 * @author      ruki
 * @file        native_page_pool.c
 *
 */

/* //////////////////////////////////////////////////////////////////////////////////////
 * trace
 */
#define TB_TRACE_MODULE_NAME            "native_page_pool"
#define TB_TRACE_MODULE_DEBUG           (1)

/* //////////////////////////////////////////////////////////////////////////////////////
 * includes
 */
#include "native_page_pool.h"

/* //////////////////////////////////////////////////////////////////////////////////////
 * macros
 */

// the native page pool ref 
#define tb_native_page_pool_ref(pool)   ((tb_page_pool_ref_t)((tb_size_t)(pool) | 0x1))

// the native page pool impl 
#define tb_native_page_pool_impl(pool)  ((tb_native_page_pool_impl_t*)((tb_size_t)(pool) & ~0x1))

/* //////////////////////////////////////////////////////////////////////////////////////
 * types
 */

// the native page data head type
typedef __tb_aligned__(TB_POOL_DATA_ALIGN) struct __tb_native_page_data_head_t
{
    // the pool reference
    tb_pointer_t                    pool;

    // the entry
    tb_list_entry_t                 entry;

    // the data head base
    tb_pool_data_head_t             base;

}__tb_aligned__(TB_POOL_DATA_ALIGN) tb_native_page_data_head_t;

/*! the native page pool impl type
 *
 * <pre>
 *        -----------       -----------               -----------
 * pool: |||  pages  | <=> |||  pages  | <=> ... <=> |||  pages  | <=> |
 *        -----------       -----------               -----------      |
 *              |                                                      |
 *              `------------------------------------------------------`
 * </pre>
 */
typedef struct __tb_native_page_pool_impl_t
{
    // the pages
    tb_list_entry_head_t            pages;

    // the page size
    tb_size_t                       pagesize;

#ifdef __tb_debug__

    // the peak size
    tb_size_t                       peak_size;

    // the total size
    tb_size_t                       total_size;

    // the occupied size
    tb_size_t                       occupied_size;

    // the malloc count
    tb_size_t                       malloc_count;

    // the ralloc count
    tb_size_t                       ralloc_count;

    // the free count
    tb_size_t                       free_count;
#endif

}tb_native_page_pool_impl_t;

/* //////////////////////////////////////////////////////////////////////////////////////
 * checker implementation
 */
#ifdef __tb_debug__
static tb_void_t tb_native_page_pool_check_data(tb_native_page_pool_impl_t* impl, tb_native_page_data_head_t const* data_head)
{
    // check
    tb_assert_and_check_return(impl && data_head);

    // done
    tb_bool_t           ok = tb_false;
    tb_byte_t const*    data = (tb_byte_t const*)&(data_head[1]);
    do
    {
        // check
        tb_assertf_break(data_head->base.debug.magic != (tb_uint16_t)~TB_POOL_DATA_MAGIC, "data have been freed: %p", data);
        tb_assertf_break(data_head->base.debug.magic == TB_POOL_DATA_MAGIC, "the invalid data: %p", data);
        tb_assertf_break(((tb_byte_t*)data)[data_head->base.size] == TB_POOL_DATA_PATCH, "data underflow");

        // ok
        ok = tb_true;

    } while (0);

    // failed? dump it
#ifdef __tb_debug__
    if (!ok) 
    {
        // dump data
        tb_pool_data_dump(data, tb_true, "[native_page_pool]: [error]: ");

        // abort
        tb_abort();
    }
#endif
}
static tb_void_t tb_native_page_pool_check_last(tb_native_page_pool_impl_t* impl)
{
    // check
    tb_assert_and_check_return(impl);

    // non-empty?
    if (!tb_list_entry_is_null(&impl->pages))
    {
        // the last entry
        tb_list_entry_ref_t data_last = tb_list_entry_last(&impl->pages);
        tb_assert_and_check_return(data_last);

        // check it
        tb_native_page_pool_check_data(impl, (tb_native_page_data_head_t*)tb_list_entry(&impl->pages, data_last));
    }
}
static tb_void_t tb_native_page_pool_check_prev(tb_native_page_pool_impl_t* impl, tb_native_page_data_head_t const* data_head)
{
    // check
    tb_assert_and_check_return(impl && data_head);

    // non-empty?
    if (!tb_list_entry_is_null(&impl->pages))
    {
        // the prev entry
        tb_list_entry_ref_t data_prev = tb_list_entry_prev(&impl->pages, (tb_list_entry_ref_t)&data_head->entry);
        tb_assert_and_check_return(data_prev);

        // not tail entry
        tb_check_return(data_prev != tb_list_entry_tail(&impl->pages));

        // check it
        tb_native_page_pool_check_data(impl, (tb_native_page_data_head_t*)tb_list_entry(&impl->pages, data_prev));
    }
}
static tb_void_t tb_native_page_pool_check_next(tb_native_page_pool_impl_t* impl, tb_native_page_data_head_t const* data_head)
{
    // check
    tb_assert_and_check_return(impl && data_head);

    // non-empty?
    if (!tb_list_entry_is_null(&impl->pages))
    {
        // the next entry
        tb_list_entry_ref_t data_next = tb_list_entry_next(&impl->pages, (tb_list_entry_ref_t)&data_head->entry);
        tb_assert_and_check_return(data_next);

        // not tail entry
        tb_check_return(data_next != tb_list_entry_tail(&impl->pages));

        // check it
        tb_native_page_pool_check_data(impl, (tb_native_page_data_head_t*)tb_list_entry(&impl->pages, data_next));
    }
}
#endif

/* //////////////////////////////////////////////////////////////////////////////////////
 * implementation
 */
tb_page_pool_ref_t tb_native_page_pool_init()
{
    // done
    tb_bool_t                   ok = tb_false;
    tb_native_page_pool_impl_t* impl = tb_null;
    do
    {
        // check
        tb_assert_static(!(sizeof(tb_native_page_data_head_t) & (TB_POOL_DATA_ALIGN - 1)));

        // make pool
        impl = (tb_native_page_pool_impl_t*)tb_native_memory_malloc0(sizeof(tb_native_page_pool_impl_t));
        tb_assert_and_check_break(impl);

        // init pages
        tb_list_entry_init(&impl->pages, tb_native_page_data_head_t, entry, tb_null);

        // init pagesize
        impl->pagesize = tb_page_size();
        tb_assert_and_check_break(impl->pagesize);

        // ok
        ok = tb_true;

    } while (0);

    // failed?
    if (!ok)
    {
        // exit it
        if (impl) tb_native_page_pool_exit(tb_native_page_pool_ref(impl));
        impl = tb_null;
    }

    // ok?
    return tb_native_page_pool_ref(impl);
}
tb_void_t tb_native_page_pool_exit(tb_page_pool_ref_t pool)
{
    // check
    tb_native_page_pool_impl_t* impl = tb_native_page_pool_impl(pool);
    tb_assert_and_check_return(impl);

    // clear it
    tb_native_page_pool_clear(pool);

    // exit it
    tb_native_memory_free(impl);
}
tb_void_t tb_native_page_pool_clear(tb_page_pool_ref_t pool)
{
    // check
    tb_native_page_pool_impl_t* impl = tb_native_page_pool_impl(pool);
    tb_assert_and_check_return(impl);

    // the iterator
    tb_iterator_ref_t iterator = tb_list_entry_itor(&impl->pages);
    tb_assert_and_check_return(iterator);

    // walk it
    tb_size_t itor = tb_iterator_head(iterator);
    while (itor != tb_iterator_tail(iterator))
    {
        // the data head
        tb_native_page_data_head_t* data_head = (tb_native_page_data_head_t*)tb_iterator_item(iterator, itor);
        tb_assert_and_check_break(data_head);

        // save next
        tb_size_t next = tb_iterator_next(iterator, itor);

        // exit data
        tb_native_page_pool_free(pool, (tb_pointer_t)&data_head[1] __tb_debug_vals__);

        // next
        itor = next;
    }
}
tb_pointer_t tb_native_page_pool_malloc(tb_page_pool_ref_t pool, tb_size_t size __tb_debug_decl__)
{
    // check
    tb_native_page_pool_impl_t* impl = tb_native_page_pool_impl(pool);
    tb_assert_and_check_return_val(impl && impl->pagesize, tb_null);

    // done 
#ifdef __tb_debug__
    tb_size_t                   patch = 1; // patch 0xcc
#else
    tb_size_t                   patch = 0;
#endif
    tb_bool_t                   ok = tb_false;
    tb_size_t                   need = sizeof(tb_native_page_data_head_t) + size + patch;
    tb_byte_t*                  data = tb_null;
    tb_byte_t*                  data_real = tb_null;
    tb_native_page_data_head_t* data_head = tb_null;
    do
    {
#ifdef __tb_debug__
        // check the last data
        tb_native_page_pool_check_last(impl);
#endif

        // make data
        data = (tb_byte_t*)tb_native_memory_malloc(need);
        tb_assert_and_check_break(data);
        tb_assert_and_check_break(!(((tb_size_t)data) & 0x1));

        // make the real data
        data_real = data + sizeof(tb_native_page_data_head_t);

        // init the data head
        data_head = (tb_native_page_data_head_t*)data;
        data_head->base.size            = (tb_uint32_t)size;
        data_head->base.cstr            = 0;
        data_head->base.free            = 0;
#ifdef __tb_debug__
        data_head->base.debug.magic     = TB_POOL_DATA_MAGIC;
        data_head->base.debug.file      = file_;
        data_head->base.debug.func      = func_;
        data_head->base.debug.line      = line_;

        // save backtrace
        tb_pool_data_save_backtrace(&data_head->base, 2);

        // make the dirty data and patch 0xcc for checking underflow
        tb_memset(data_real, TB_POOL_DATA_PATCH, size + 1);
#endif

        // save pool reference for checking data range
        data_head->pool = (tb_pointer_t)pool;

        // save the data to the pages
        tb_list_entry_insert_tail(&impl->pages, &data_head->entry);

#ifdef __tb_debug__
        // update the occupied size
        tb_size_t occupied_size = need - sizeof(tb_pool_data_debug_head_t);
        impl->occupied_size += occupied_size;

        // update total size
        impl->total_size    += size;

        // update peak size
        if (occupied_size > impl->peak_size) impl->peak_size = occupied_size;

        // update ralloc count
        impl->malloc_count++;
#endif

        // ok
        ok = tb_true;

    } while (0);

    // failed?
    if (!ok)
    {
        // exit the data
        if (data) tb_native_memory_free(data);
        data = tb_null;
        data_real = tb_null;
    }

    // ok?
    return (tb_pointer_t)data_real;
}
tb_pointer_t tb_native_page_pool_ralloc(tb_page_pool_ref_t pool, tb_pointer_t data, tb_size_t size __tb_debug_decl__)
{
    // check
    tb_native_page_pool_impl_t* impl = tb_native_page_pool_impl(pool);
    tb_assert_and_check_return_val(impl && impl->pagesize, tb_null);

    // done 
#ifdef __tb_debug__
    tb_size_t                   patch = 1; // patch 0xcc
#else
    tb_size_t                   patch = 0;
#endif
    tb_bool_t                   ok = tb_false;
    tb_bool_t                   removed = tb_false;
    tb_size_t                   need = sizeof(tb_native_page_data_head_t) + size + patch;
    tb_byte_t*                  data_real = tb_null;
    tb_native_page_data_head_t* data_head = tb_null;
    do
    {
        // no data?
        tb_assert_and_check_break(data);

        // the data head
        data_head = &(((tb_native_page_data_head_t*)data)[-1]);
        tb_assertf_break(data_head->base.debug.magic != (tb_uint16_t)~TB_POOL_DATA_MAGIC, "ralloc freed data: %p", data);
        tb_assertf_break(data_head->base.debug.magic == TB_POOL_DATA_MAGIC, "ralloc invalid data: %p", data);
        tb_assertf_and_check_break(data_head->pool == (tb_pointer_t)pool, "the data: %p not belong to pool: %p", data, pool);
        tb_assertf_break(((tb_byte_t*)data)[data_head->base.size] == TB_POOL_DATA_PATCH, "data underflow");

#ifdef __tb_debug__
        // check the last data
        tb_native_page_pool_check_last(impl);

        // check the prev data
        tb_native_page_pool_check_prev(impl, data_head);

        // check the next data
        tb_native_page_pool_check_next(impl, data_head);
#endif

        // remove the data from the pages
        tb_list_entry_remove(&impl->pages, &data_head->entry);
        removed = tb_true;

        // ralloc data
        data = (tb_byte_t*)tb_native_memory_ralloc(data_head, need);
        tb_assert_and_check_break(data);
        tb_assert_and_check_break(!(((tb_size_t)data) & 0x1));

        // update the real data
        data_real = data + sizeof(tb_native_page_data_head_t);

        // update the data head
        data_head = (tb_native_page_data_head_t*)data;
        data_head->base.size            = (tb_uint32_t)size;
#ifdef __tb_debug__
        data_head->base.debug.file      = file_;
        data_head->base.debug.func      = func_;
        data_head->base.debug.line      = line_;

        // check
        tb_assertf_break(data_head->base.debug.magic == TB_POOL_DATA_MAGIC, "ralloc data have been changed: %p", data);

        // update backtrace
        tb_pool_data_save_backtrace(&data_head->base, 2);

        // make the dirty data and patch 0xcc for checking underflow
        tb_memset(data_real, TB_POOL_DATA_PATCH, size + 1);
#endif

        // save the data to the pages
        tb_list_entry_insert_tail(&impl->pages, &data_head->entry);
        removed = tb_false;

#ifdef __tb_debug__
        // update the occupied size
        tb_size_t occupied_size = need - sizeof(tb_pool_data_debug_head_t);
        impl->occupied_size += occupied_size;

        // update total size
        impl->total_size    += size;

        // update peak size
        if (occupied_size > impl->peak_size) impl->peak_size = occupied_size;

        // update ralloc count
        impl->ralloc_count++;
#endif

        // ok
        ok = tb_true;

    } while (0);

    // failed?
    if (!ok)
    {
        // restore data to pages
        if (data_head && removed) tb_list_entry_insert_tail(&impl->pages, &data_head->entry);

        // clear it
        data = tb_null;
        data_real = tb_null;
    }

    // ok?
    return (tb_pointer_t)data_real;
}
tb_bool_t tb_native_page_pool_free(tb_page_pool_ref_t pool, tb_pointer_t data __tb_debug_decl__)
{
    // check
    tb_native_page_pool_impl_t* impl = tb_native_page_pool_impl(pool);
    tb_assert_and_check_return_val(impl && impl->pagesize, tb_false);

    // done
    tb_bool_t                   ok = tb_false;
    tb_native_page_data_head_t* data_head = tb_null;
    do
    {
        // no data?
        tb_assert_and_check_break(data);

        // the data head
        data_head = &(((tb_native_page_data_head_t*)data)[-1]);
        tb_assertf_break(data_head->base.debug.magic != (tb_uint16_t)~TB_POOL_DATA_MAGIC, "double free data: %p", data);
        tb_assertf_break(data_head->base.debug.magic == TB_POOL_DATA_MAGIC, "free invalid data: %p", data);
        tb_assertf_and_check_break(data_head->pool == (tb_pointer_t)pool, "the data: %p not belong to pool: %p", data, pool);
        tb_assertf_break(((tb_byte_t*)data)[data_head->base.size] == TB_POOL_DATA_PATCH, "data underflow");

#ifdef __tb_debug__
        // check the last data
        tb_native_page_pool_check_last(impl);

        // check the prev data
        tb_native_page_pool_check_prev(impl, data_head);

        // check the next data
        tb_native_page_pool_check_next(impl, data_head);
#endif

        // remove the data from the pages
        tb_list_entry_remove(&impl->pages, &data_head->entry);

#ifdef __tb_debug__
        // clear magic for checking double-free
        data_head->base.debug.magic = (tb_uint16_t)~TB_POOL_DATA_MAGIC;
#endif

        // free it
        tb_native_memory_free(data_head);

#ifdef __tb_debug__
        // update free count
        impl->free_count++;
#endif

        // ok
        ok = tb_true;

    } while (0);

    // ok?
    return ok;
}
#ifdef __tb_debug__
tb_void_t tb_native_page_pool_dump(tb_page_pool_ref_t pool)
{
    // check
    tb_native_page_pool_impl_t* impl = tb_native_page_pool_impl(pool);
    tb_assert_and_check_return(impl);

    // trace
    tb_trace_i("======================================================================");

    // exit all pages
    tb_for_all_if (tb_native_page_data_head_t*, data_head, tb_list_entry_itor(&impl->pages), data_head)
    {
        // check it
        tb_native_page_pool_check_data(impl, data_head);

        // trace
        tb_trace_e("leak: %p", &data_head[1]);

        // dump data
        tb_pool_data_dump((tb_byte_t const*)&data_head[1], tb_false, "[native_page_pool]: [error]: ");
    }

    // trace debug info
    tb_trace_i("peak_size: %lu",            impl->peak_size);
    tb_trace_i("wast_rate: %llu/10000",     impl->occupied_size? (((tb_hize_t)impl->occupied_size - impl->total_size) * 10000) / (tb_hize_t)impl->occupied_size : 0);
    tb_trace_i("free_count: %lu",           impl->free_count);
    tb_trace_i("malloc_count: %lu",         impl->malloc_count);
    tb_trace_i("ralloc_count: %lu",         impl->ralloc_count);

    // trace
    tb_trace_i("======================================================================");
}
#endif
