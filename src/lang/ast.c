/*
 * Copyright 2015-2017 Tobias Waldekranz <tobias@waldekranz.com>
 *
 * This file is part of ply.
 *
 * ply is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation, under the terms of version 2 of the
 * License.
 *
 * ply is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
 * License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with ply.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <assert.h>
#include <ctype.h>
#include <inttypes.h>
#include <search.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ply/ast.h>

#include "parse.h"
#include "lex.h"

const char *op_str(op_t op)
{
#define OP(_type, _bpf_type, _typestr) [_type] = _typestr,
	static const char *strs[] = {
		OP_TABLE
	};
#undef OP

	return strs[op];
}

const char *type_str(type_t type)
{
#define TYPE(_type, _typestr) [_type] = _typestr,
	static const char *strs[] = {
		NODE_TYPE_TABLE
	};
#undef TYPE

	return strs[type];
}

const char *loc_str(loc_t loc)
{
	switch (loc) {
	case LOC_NOWHERE:
		return "nowhere";
	case LOC_VIRTUAL:
		return "virtual";
	case LOC_REG:
		return "reg";
	case LOC_STACK:
		return "stack";
	}

	return "UNKNOWN";
}

static int _has_next(node_t *n)
{
	if (n->next)
		return 1;
	else if (n->parent) {
		if (n->parent->type == TYPE_BINOP &&
		    n == n->parent->binop.left)
			return 1;
		else if (n->parent->type == TYPE_ASSIGN &&
			 n == n->parent->assign.lval)
			return 1;
		else if (n->parent->type == TYPE_IF &&
			 n == n->parent->iff.cond)
			return 1;
	}

	return 0;
}

static void _indent(int *indent, node_t *n)
{
	node_t *p;
	int i, j;

	for (i = 0; i < *indent; i++) {
		for (p = n, j = 0; j < (*indent - i); j++)
			p = p->parent;

		fprintf(stderr, "%c   ", _has_next(p) ? '|' : ' ');
	}

	fprintf(stderr, "%c-> ", _has_next(n) ? '|' : '`');
	(*indent)++;
}

static int _unindent(node_t *n, void *_indent)
{
	int *indent = _indent;

	(*indent)--;
	return 0;
}

static void _fputs_escape(FILE *fp, const char *s)
{
	fputc('\"', fp);
	for (; *s; s++) {
		if (isprint(*s)) {
			fputc(*s, fp);
			continue;
		}

		fputc('\\', fp);

		switch (*s) {
		case '\n':
			fputc('n', fp);
			break;
		case '\r':
			fputc('r', fp);
			break;
		case '\t':
			fputc('t', fp);
			break;
		default:
			fprintf(fp, "x%2.2x", *s);
			break;
		}
	}
	fputc('\"', fp);
}

int node_fdump(node_t *n, FILE *fp)
{
	switch (n->type) {
	case TYPE_NONE:
	case TYPE_SCRIPT:
	case TYPE_METHOD:
	case TYPE_IF:
	case TYPE_BREAK:
	case TYPE_CONTINUE:
	case TYPE_RETURN:
	case TYPE_NOT:
	case TYPE_REC:
		fprintf(fp, "<%s> ", type_str(n->type));
		break;
		
	case TYPE_PROBE:
	case TYPE_ASSIGN:
	case TYPE_MAP:
	case TYPE_VAR:
		fprintf(fp, "%s ", n->string);
		break;

	case TYPE_BINOP:
		fprintf(fp, "%s ", op_str(n->binop.op));
		break;

	case TYPE_UNROLL:
		fprintf(fp, "unroll (%"PRId64") ", n->unroll.count);
		break;

	case TYPE_CALL:
		fprintf(fp, "%s.%s ", n->call.module? : "<auto>", n->string);
		break;

	case TYPE_INT:
		fprintf(fp, "%#" PRIx64 " ", n->integer);
		break;
		
	case TYPE_STR:
		_fputs_escape(stderr, n->string);
		break;

	case TYPE_STACK:
		assert(0);
		break;
	}

	fprintf(fp, "(type:%s/%s size:0x%zx loc:%s",
		type_str(n->type), type_str(n->dyn->type),
		n->dyn->size, loc_str(n->dyn->loc));

	switch (n->dyn->loc) {
	case LOC_NOWHERE:
	case LOC_VIRTUAL:
		break;
	case LOC_REG:
		fprintf(fp, "/%d", n->dyn->reg);
		break;
	case LOC_STACK:
		fprintf(fp, "/-0x%zx", -n->dyn->addr);
		break;
	}

	fputs(")", fp);
	return 0;
}

int node_sdump(node_t *n, char *buf, size_t sz)
{
	FILE *fp = fmemopen(buf, sz, "w");
	int err;

	err = node_fdump(n, fp);
	fclose(fp);
	return err;
}

static int _node_ast_dump(node_t *n, void *indent)
{
	_indent((int *)indent, n);

	node_fdump(n, stderr);
	fputs("\n", stderr);
	return 0;
}

void node_ast_dump(node_t *n)
{
	int indent = 0;

	fprintf(stderr, "ast:\n");
	node_walk(n, _node_ast_dump, _unindent, &indent);
}


node_t *node_get_parent_of_type(type_t type, node_t *n)
{
	for (; n && n->type != type; n = n->parent);
	return n;
}

node_t *node_get_stmt(node_t *n) {
	for (; n; n = n->parent) {
		if (n->parent->type == TYPE_PROBE)
			return n;
	}

	return NULL;
}

node_t *node_get_probe(node_t *n)
{
	return node_get_parent_of_type(TYPE_PROBE, n);
}

pvdr_t *node_get_pvdr(node_t *n)
{
	node_t *probe = node_get_probe(n);

	return probe ? probe->dyn->probe.pvdr : NULL;
}

node_t *node_get_script(node_t *n)
{
	return node_get_parent_of_type(TYPE_SCRIPT, n);
}

int node_probe_reg_get(node_t *probe, int dynamic)
{
	int *pool, *compl;
	int reg;

	if (dynamic) {
		pool  = &probe->dyn->probe.dyn_regs;
		compl = &probe->dyn->probe.stat_regs;
	} else {
		pool  = &probe->dyn->probe.stat_regs;
		compl = &probe->dyn->probe.dyn_regs;
	}		
	
	for (reg = BPF_REG_6; reg < BPF_REG_9; reg++) {
		if ((*pool) & (*compl) & (1 << reg)) {
			*pool &= ~(1 << reg);
			return reg;
		}
	}

	return -1;
}

ssize_t node_probe_stack_get(node_t *probe, size_t size)
{
	probe->dyn->probe.sp -= size;
	return probe->dyn->probe.sp;
}


node_t *node_new(type_t type) {
	node_t *n = calloc(1, sizeof(*n));

	assert(n);
	n->type = type;

	/* maps and vars have shared dyns allocated in the symtable */
	if (n->type != TYPE_MAP && n->type != TYPE_VAR) {
		n->dyn = calloc(1, sizeof(*n->dyn));
		assert(n->dyn);
	}
				
	return n;
}

node_t *node_str_new(char *val)
{
	node_t *n = node_new(TYPE_STR);

	n->string = val;
	return n;
}

node_t *node_int_new(int64_t val)
{
	node_t *n = node_new(TYPE_INT);

	n->integer = val;
	return n;
}

node_t *node_rec_new(node_t *vargs)
{
	node_t *c, *n = node_new(TYPE_REC);

	n->rec.vargs = vargs;

	node_foreach(c, vargs) {
		c->parent = n;
		n->rec.n_vargs++;
	}
	return n;
}

node_t *node_map_new(char *name, node_t *rec)
{
	node_t *n = node_new(TYPE_MAP);

	if (!rec)
		rec = node_rec_new(node_str_new(strdup("")));

	n->string  = name;
	n->map.rec = rec;

	rec->parent = n;
	return n;
}

node_t *node_var_new(char *name)
{
	node_t *n = node_new(TYPE_VAR);

	n->string = name;
	return n;
}

node_t *node_not_new(node_t *expr)
{
	node_t *n = node_new(TYPE_NOT);

	n->not = expr;

	expr->parent = n;
	return n;
}

node_t *node_binop_new(node_t *left, op_t op, node_t *right)
{
	node_t *n = node_new(TYPE_BINOP);

	n->binop.op    = op;
	n->binop.left  = left;
	n->binop.right = right;

	left->parent  = n;
	right->parent = n;
	return n;
}

node_t *node_assign_new(node_t *lval, node_t *expr)
{
	node_t *n = node_new(TYPE_ASSIGN);

	n->string = strdup("=");
	n->assign.lval = lval;
	n->assign.expr = expr;

	lval->parent = n;
	if (expr)
		expr->parent = n;
	return n;
}

node_t *node_method_new(node_t *map, node_t *call)
{
	node_t *n = node_new(TYPE_METHOD);

	call->call.module = strdup("method");
	n->method.map  = map;
	n->method.call = call;

	map->parent  = n;
	call->parent = n;
	return n;	
}

node_t *node_call_new(char *module, char *func, node_t *vargs)
{
	node_t *c, *n = node_new(TYPE_CALL);

	n->string = func;
	n->call.module = module;
	n->call.vargs = vargs;

	node_foreach(c, vargs) {
		c->parent = n;
		n->call.n_vargs++;
	}
	return n;
}

node_t *node_if_new(node_t *cond, node_t *then, node_t *els)
{
	node_t *c, *n = node_new(TYPE_IF);

	n->iff.cond = cond;
	n->iff.then = then;
	n->iff.els  = els;

	cond->parent = n;

	node_foreach(c, then) {
		c->parent = n;

		if (!c->next)
			n->iff.then_last = c;
	}

	if (els) {
		node_foreach(c, els)
			c->parent = n;
	}
	return n;
}

node_t *node_unroll_new(int64_t count, node_t *stmts)
{
	node_t *c, *n = node_new(TYPE_UNROLL);

	n->unroll.count = count;
	n->unroll.stmts = stmts;

	node_foreach(c, stmts)
		c->parent = n;
	return n;
}

node_t *node_probe_new(char *pspec, node_t *pred,
			     node_t *stmts)
{
	node_t *c, *n = node_new(TYPE_PROBE);

	n->string = pspec;
	n->probe.pred   = pred;
	n->probe.stmts  = stmts;

	if (pred)
		pred->parent = n;

	node_foreach(c, stmts)
		c->parent = n;
	return n;
}

node_t *node_script_new(node_t *probes)
{
	node_t *c, *n = node_new(TYPE_SCRIPT);

	n->script.probes = probes;

	node_foreach(c, probes)
		c->parent = n;
	return n;
}

node_t *node_script_parse(FILE *fp)
{
	node_t *script = NULL;
	yyscan_t scanner;
	
	if (yylex_init(&scanner))
		return NULL;

	yyset_in(fp, scanner);
	yyparse(&script, scanner);
 
	yylex_destroy(scanner); 
	return script;
}

static int _node_free(node_t *n, void *_null)
{
	switch (n->type) {
	case TYPE_CALL:
		if (n->call.module)
			free(n->call.module);
		/* fall-through */
	case TYPE_PROBE:
	case TYPE_ASSIGN:
	case TYPE_MAP:
	case TYPE_STR:
		free(n->string);
		break;

	default:
		break;
	}

	if (n->type != TYPE_MAP && n->type != TYPE_VAR)
		free(n->dyn);

	free(n);
	return 0;
}

void node_free(node_t *n)
{
	node_walk(n, NULL, _node_free, NULL);
}

static int _node_walk_list(node_t *head,
			 int (*pre) (node_t *n, void *ctx),
			 int (*post)(node_t *n, void *ctx), void *ctx)
{
	node_t *elem, *next = head;
	int err = 0;
	
	for (elem = next; !err && elem;) {
		next = elem->next;
		err = node_walk(elem, pre, post, ctx);
		elem = next;
	}

	return err;
}


int node_walk(node_t *n,
	    int (*pre) (node_t *n, void *ctx),
	    int (*post)(node_t *n, void *ctx), void *ctx)
{
#define do_list(_head) err = _node_walk_list(_head, pre, post, ctx); if (err) return err
#define do_walk(_node) err =       node_walk(_node, pre, post, ctx); if (err) return err
	int err;

	err = pre ? pre(n, ctx) : 0;
	if (err)
		return err;
	
	switch (n->type) {
	case TYPE_SCRIPT:
		do_list(n->script.probes);
		break;

	case TYPE_PROBE:
		if (n->probe.pred)
			do_walk(n->probe.pred);
		do_list(n->probe.stmts);
		break;

	case TYPE_IF:
		do_walk(n->iff.cond);
		do_list(n->iff.then);

		if (n->iff.els)
			do_list(n->iff.els);
		break;

	case TYPE_UNROLL:
		do_list(n->unroll.stmts);
		break;

	case TYPE_CALL:
		do_list(n->call.vargs);
		break;

	case TYPE_METHOD:
		do_walk(n->method.map);
		do_walk(n->method.call);
		break;

	case TYPE_ASSIGN:
		do_walk(n->assign.lval);
		if (n->assign.expr)
			do_walk(n->assign.expr);
		break;

	case TYPE_BINOP:
		do_walk(n->binop.left);
		do_walk(n->binop.right);
		break;

	case TYPE_NOT:
		do_walk(n->not);
		break;

	case TYPE_MAP:
		do_walk(n->map.rec);
		break;

	case TYPE_REC:
		do_list(n->rec.vargs);
		break;

	case TYPE_NONE:
		return -1;

	default:
		break;
	}

	return post ? post(n, ctx) : 0;
}

		
