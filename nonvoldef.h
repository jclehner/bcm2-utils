/**
 * bcm2-utils
 * Copyright (C) 2016 Joseph Lehner <joseph.c.lehner@gmail.com>
 *
 * bcm2-utils is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * bcm2-utils is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with bcm2-utils.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "nonvol2.h"

#define NV_VAR(type, name, ...) { name, make_shared<type>(__VA_ARGS__) }
#define NV_VARN(type, name, ...) { name, nv_compound_rename(make_shared<type >(__VA_ARGS__), name) }
#define NV_VAR2(type, name, ...) { name, sp<type>(new type(__VA_ARGS__)) }
#define NV_VARN2(type, name, ...) { name, nv_compound_rename(sp<type>(new type(__VA_ARGS__)), name) }
#define NV_VAR3(cond, type, name, ...) { name, nv_val_disable<type>(shared_ptr<type>(new type(__VA_ARGS__)), !(cond)) }
#define NV_VARN3(cond, type, name, ...) { name, nv_compound_rename(nv_val_disable<type>(shared_ptr<type>(new type(__VA_ARGS__)), !(cond)), name) }


#define NV_ARRAY(type, count) nv_array<type, count>

#define COMMA() ,

#define NV_GROUP(group, ...) make_shared<group>(__VA_ARGS__)
#define NV_GROUP_DEF_CLONE(type) \
		virtual type* clone() const override \
		{ return new type(*this); }
#define NV_GROUP_DEF_CTOR_AND_CLONE(type, magic, pretty) \
		type() : nv_group(magic, pretty) {} \
		\
		NV_GROUP_DEF_CLONE(type)
#define NV_COMPOUND_DEF_CTOR_AND_TYPE(ttype, tname) \
		ttype(const string& name = "") : nv_compound(false) \
		{ nv_compound::rename(name); } \
		\
		virtual string type() const override \
		{ return tname; }
