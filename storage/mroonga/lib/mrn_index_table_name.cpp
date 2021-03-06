/* -*- c-basic-offset: 2 -*- */
/*
  Copyright(C) 2011 Kentoku SHIBA
  Copyright(C) 2011-2012 Kouhei Sutou <kou@clear-code.com>

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include <mrn_mysql.h>

#include "mrn_index_table_name.hpp"

// for debug
#define MRN_CLASS_NAME "mrn::IndexTableName"

namespace mrn {
  IndexTableName::IndexTableName(const char *table_name,
                                 const char *mysql_index_name)
    : table_name_(table_name),
      mysql_index_name_(mysql_index_name) {
    uchar encoded_mysql_index_name_multibyte[MRN_MAX_KEY_SIZE];
    const uchar *mysql_index_name_multibyte =
      reinterpret_cast<const uchar *>(mysql_index_name_);
    encode(encoded_mysql_index_name_multibyte,
           encoded_mysql_index_name_multibyte + MRN_MAX_KEY_SIZE,
           mysql_index_name_multibyte,
           mysql_index_name_multibyte + strlen(mysql_index_name_));
    snprintf(name_, MRN_MAX_KEY_SIZE,
             "%s-%s", table_name_, encoded_mysql_index_name_multibyte);
    length_ = strlen(name_);
  }

  const char *IndexTableName::c_str() {
    return name_;
  }

  size_t IndexTableName::length() {
    return length_;
  }

  uint IndexTableName::encode(uchar *encoded_start,
                              uchar *encoded_end,
                              const uchar *mysql_string_start,
                              const uchar *mysql_string_end) {
    MRN_DBUG_ENTER_METHOD();
    my_charset_conv_mb_wc mb_wc = system_charset_info->cset->mb_wc;
    my_charset_conv_wc_mb wc_mb = my_charset_filename.cset->wc_mb;
    DBUG_PRINT("info", ("mroonga: in=%s", mysql_string_start));
    encoded_end--;
    uchar *encoded = encoded_start;
    const uchar *mysql_string = mysql_string_start;
    while (mysql_string < mysql_string_end && encoded < encoded_end) {
      my_wc_t wc;
      int mb_wc_converted_length;
      int wc_mb_converted_length;
      mb_wc_converted_length =
        (*mb_wc)(NULL, &wc, mysql_string, mysql_string_end);
      if (mb_wc_converted_length > 0) {
        wc_mb_converted_length = (*wc_mb)(NULL, wc, encoded, encoded_end);
        if (wc_mb_converted_length <= 0) {
          break;
        }
      } else if (mb_wc_converted_length == MY_CS_ILSEQ) {
        *encoded = *mysql_string;
        mb_wc_converted_length = 1;
        wc_mb_converted_length = 1;
      } else {
        break;
      }
      mysql_string += mb_wc_converted_length;
      encoded += wc_mb_converted_length;
    }
    *encoded = '\0';
    DBUG_PRINT("info", ("mroonga: out=%s", encoded_start));
    DBUG_RETURN(encoded - encoded_start);
  }
}
