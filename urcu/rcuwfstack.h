/*
 * rcuwfstack.h
 *
 * Userspace RCU library - RCU Stack with Wait-Free push, Blocking pop.
 *
 * Copyright 2010 - Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#if (!defined(_GNU_SOURCE) && !defined(_LGPL_SOURCE))
#error "Dynamic loader LGPL wrappers not implemented yet"
#endif

struct rcu_wfs_node {
	struct rcu_wfs_node *next;
};

struct rcu_wfs_stack {
	struct rcu_wfs_node *head;
	struct rcu_wfs_node end;
};

void rcu_wfs_node_init(struct rcu_wfs_node *node)
{
	node->next = NULL;
}

void rcu_wfs_init(struct rcu_wfs_stack *s)
{
	s->head = &s->end;
	rcu_wfs_node_init(&s->end);
}

void rcu_wfs_push(struct rcu_wfs_stack *s, struct rcu_wfs_node *node)
{
	struct rcu_wfs_node *old_head;

	/*
	 * uatomic_xchg() implicit memory barrier orders earlier stores to node
	 * (setting it to NULL) before publication.
	 */
	old_head = uatomic_xchg(&s->head, node);
	/*
	 * At this point, dequeuers see a NULL node->next, they should busy-wait
	 * until node->next is set to old_head.
	 */
	STORE_SHARED(node->next, old_head);
}

/*
 * The caller must wait for a grace period before freeing the returned node.
 * Returns NULL if stack is empty.
 *
 * cmpxchg is protected from ABA races by holding a RCU read lock between
 * s->head read and cmpxchg modifying s->head and requiring that dequeuers wait
 * for a grace period before freeing the returned node.
 *
 * TODO: implement adaptative busy-wait and wait/wakeup scheme rather than busy
 * loops. Better for UP.
 */
struct rcu_wfs_node *
rcu_wfs_pop(struct rcu_wfs_stack *s)
{
	rcu_read_lock();
	for (;;) {
		struct rcu_wfs_node *head = rcu_dereference(s->head);

		if (head != &s->end) {
			struct rcu_wfs_node *next = rcu_dereference(head->next);

			/* Retry while head is being set by push(). */
			if (!next)
				continue;

			if (uatomic_cmpxchg(&s->head, head, next) == head) {
				rcu_read_unlock();
				return head;
			} else {
				/* Concurrent modification. Retry. */
				continue;
			}
		} else {
			/* Empty stack */
			rcu_read_unlock();
			return NULL;
		}
	}
}
