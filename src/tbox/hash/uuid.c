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
 * *
 * @author      ruki
 * @file        uuid.c
 * @ingroup     hash
 *
 */

/* //////////////////////////////////////////////////////////////////////////////////////
 * includes
 */
#include "uuid.h"
#include "bkdr.h"
#include "../utils/utils.h"

/* //////////////////////////////////////////////////////////////////////////////////////
 * implementation
 */
tb_bool_t tb_uuid_make(tb_byte_t uuid[16], tb_char_t const* name)
{
    // check
    tb_assert_and_check_return_val(uuid, tb_false);

    // we only generate it using a simple hashing function for speed if name is supplied 
    if (name)
    {
        // generate hash values
        tb_uint32_t h0 = tb_bkdr_make_from_cstr(name, 'g');
        tb_uint32_t h1 = tb_bkdr_make_from_cstr(name, 'u');
        tb_uint32_t h2 = tb_bkdr_make_from_cstr(name, 'i');
        tb_uint32_t h3 = tb_bkdr_make_from_cstr(name, 'd');

        // fill uuid
        tb_bits_set_u32_be(uuid + 0,    h0);
        tb_bits_set_u32_be(uuid + 4,    h1);
        tb_bits_set_u32_be(uuid + 8,    h2);
        tb_bits_set_u32_be(uuid + 12,   h3);
    }

    // ok
    return tb_true;
}
tb_char_t const* tb_uuid_make_cstr(tb_char_t uuid_cstr[37], tb_char_t const* name)
{
    // check
    tb_assert_and_check_return_val(uuid_cstr, tb_null);

    // make uuid bytes
    tb_byte_t uuid[16];
    if (!tb_uuid_make(uuid, name)) return tb_null;

    // make uuid string
	tb_long_t size = tb_snprintf(   uuid_cstr
                                ,   37
                                ,   "%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X"
                                ,   uuid[0], uuid[1], uuid[2], uuid[3]
                                ,   uuid[4], uuid[5]
                                ,   uuid[6], uuid[7]
                                ,   uuid[8], uuid[9]
                                ,   uuid[10], uuid[11], uuid[12], uuid[13], uuid[14], uuid[15]);
    tb_assert_and_check_return_val(size == 36, tb_null);

    // end
    uuid_cstr[36] = '\0';

    // ok
    return uuid_cstr;
}
