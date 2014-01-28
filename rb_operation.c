
/*
** Copyright (C) 2014 Eneo Tecnologia S.L.
** Author: Eugenio Perez <eupm90@gmail.com>
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License Version 2 as
** published by the Free Software Foundation.  You may not use, modify or
** distribute this program under any other version of the GNU General
** Public License.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
*/

#include "rb_log.h"
#include "rb_operation.h"
#include "rb_libmatheval.h"

#include "matheval.h"

static void prepare_mv(struct monitor_value *mv)
{
	mv->type = op_type;
}

static void do_operation(struct monitor_value *mv, struct monitor_values_tree *mv_tree, void *evaluator)
{
	char **names;
	int count;

	struct monitor_value mv_tofind;
	memcpy(&mv_tofind,mv,sizeof(*mv));

	evaluator_get_variables(evaluator,&names,&count);

	for(int i=0;i<count;++i)
	{
		mv_tofind.name = names[i];
		const struct monitor_value *tree_mv = find_monitor_value(mv_tree,&mv_tofind);
		(void)tree_mv;
	}
}

bool operation_get_response(struct monitor_value *mv,void *_mv_tree, const void *_operation){
	double number; // FIXME: Delete it
	assert(_operation);
	const char * operation = _operation;
	struct monitor_values_tree *mv_tree = _mv_tree;

	prepare_mv(mv);
	void * const evaluator = evaluator_create ((char *)operation);
	if(NULL==evaluator)
	{
		Log(LOG_ERR,"%s is not a valid operation.",operation);
	}
	else
	{
		do_operation(mv,mv_tree,evaluator);
	}

	return true;
}
