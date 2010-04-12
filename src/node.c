#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <lua.h>

#include "node.h"
#include "mem.h"
#include "support.h"
#include "path.h"
#include "context.h"
#include "session.h"

#include "nodelinktree.inl"

/* */
#ifdef BAM_FAMILY_WINDOWS
/* on windows, we need to handle that filenames with mixed casing are the same.
	to solve this we have this table that converts all uppercase letters.
	in addition to this, we also convert all '\' to '/' to remove that
	ambiguity
*/
static const unsigned char tolower_table[256] = {
0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47,
48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63,
64, 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o',
'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z', 91, '/', 93, 94, 95,
96, 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o',
'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z', 123, 124, 125, 126, 127,
128, 129, 130, 131, 132, 133, 134, 135, 136, 137, 138, 139, 140, 141, 142, 143,
144, 145, 146, 147, 148, 149, 150, 151, 152, 153, 154, 155, 156, 157, 158, 159,
160, 161, 162, 163, 164, 165, 166, 167, 168, 169, 170, 171, 172, 173, 174, 175,
176, 177, 178, 179, 180, 181, 182, 183, 184, 185, 186, 187, 188, 189, 190, 191,
192, 193, 194, 195, 196, 197, 198, 199, 200, 201, 202, 203, 204, 205, 206, 207,
208, 209, 210, 211, 212, 213, 214, 215, 216, 217, 218, 219, 220, 221, 222, 223,
224, 225, 226, 227, 228, 229, 230, 231, 232, 233, 234, 235, 236, 237, 238, 239,
240, 241, 242, 243, 244, 245, 246, 247, 248, 249, 250, 251, 252, 253, 254, 255};

static unsigned int string_hash(const char *str_in)
{
	unsigned int h = 0;
	const unsigned char *str = (const unsigned char *)str_in;
	for (; *str; str++)
		h = 31*h + tolower_table[*str];
	return h;
}
#else
/* normal unix version */
static unsigned int string_hash(const char *str)
{
	unsigned int h = 0;
	for (; *str; str++)
		h = 31*h + *str;
	return h;
}
#endif

/* */
struct GRAPH *node_create_graph(struct HEAP *heap)
{
	/* allocate graph structure */
	struct GRAPH *graph = (struct GRAPH*)mem_allocate(heap, sizeof(struct GRAPH));
	if(!graph)
		return (struct GRAPH *)0x0;

	/* init */
	graph->heap = heap;
	return graph; 
}

/* creates a node */
int node_create(struct NODE **nodeptr, struct GRAPH *graph, const char *filename, const char *label, const char *cmdline)
{
	struct NODE *node;
	struct NODETREELINK *link;
	int sn;
	unsigned hashid = string_hash(filename);

	/* check arguments */
	if(!path_isnice(filename))
	{
		printf("%s: error: adding non nice path '%s'. this causes problems with dependency lookups\n", session.name, filename);
		return NODECREATE_NOTNICE;
	}
	
	if(cmdline && !label)
	{
		printf("%s: error: adding job '%s' with command but no label\n", session.name, filename);
		return NODECREATE_INVALID_ARG;
	}
	else if(!cmdline && label)
	{
		printf("%s: error: adding job '%s' with label but no command\n", session.name, filename);
		return NODECREATE_INVALID_ARG;
	}
	
	/* zero out the return pointer */
	*nodeptr = (struct NODE *)0x0;
		
	/* search for the node */
	link = nodelinktree_find_closest(graph->nodehash[hashid&0xffff], hashid);
	if(link && link->node->hashid == hashid)
	{
		/* we are allowed to create a new node from a node that doesn't
			have a job assigned to it*/
		if(link->node->cmdline || cmdline == NULL)
			return NODECREATE_EXISTS;
		node = link->node;
	}
	else
	{
		/* allocate and set pointers */
		node = (struct NODE *)mem_allocate(graph->heap, sizeof(struct NODE));
		
		node->graph = graph;
		node->id = graph->num_nodes++;
		node->timestamp_raw = file_timestamp(filename);
		node->timestamp = node->timestamp_raw;
		
		/* set filename */
		node->filename_len = strlen(filename)+1;
		node->filename = (char *)mem_allocate(graph->heap, node->filename_len);
		memcpy(node->filename, filename, node->filename_len);
		node->hashid = string_hash(filename);
		
		/* add to hashed tree */
		nodelinktree_insert(&graph->nodehash[node->hashid&0xffff], link, node);

		/* add to list */
		if(graph->last) graph->last->next = node;
		else graph->first = node;
		graph->last = node;		
	}
	
	/* set command and label */
	if(cmdline)
	{
		sn = strlen(cmdline)+1;
		node->cmdline = (char *)mem_allocate(graph->heap, sn);
		memcpy(node->cmdline, cmdline, sn);
		node->cmdhash = string_hash(cmdline);
		node->cachehash = node->cmdhash;
		
		/* set label */
		sn = strlen(label)+1;
		node->label = (char *)mem_allocate(graph->heap, sn);
		memcpy(node->label, label, sn);
	}
	
	/* return new node */
	*nodeptr = node;
	return NODECREATE_OK;
}

/* finds a node based apun the filename */
struct NODE *node_find(struct GRAPH *graph, const char *filename)
{
	unsigned int hashid = string_hash(filename);
	struct NODETREELINK *link;
	link = nodelinktree_find_closest(graph->nodehash[hashid&0xffff], hashid);
	if(link && link->node->hashid == hashid)
		return link->node;
	return NULL;
}

/* this will return the existing node or create a new one */
struct NODE *node_get(struct GRAPH *graph, const char *filename) 
{
	struct NODE *node = node_find(graph, filename);
	
	if(!node)
	{
		if(node_create(&node, graph, filename, 0, 0) == NODECREATE_OK)
			return node;
	}
	return node;
}

struct NODE *node_add_dependency_withnode(struct NODE *node, struct NODE *depnode)
{
	struct NODELINK *dep;
	struct NODELINK *parent;
	struct NODETREELINK *treelink;
	
	/* make sure that the node doesn't try to depend on it self */
	if(depnode == node)
	{
		if(node->cmdline)
		{
			printf("error: node '%s' is depended on itself and is produced by a job\n", node->filename);
			return (struct NODE*)0x0;
		}
		
		return node;
	}
	
	/* check if we are already dependent on this node */
	treelink = nodelinktree_find_closest(node->deproot, depnode->hashid);
	if(treelink != NULL && treelink->node->hashid == depnode->hashid)
		return depnode;

	/* create and add dependency link */
	dep = (struct NODELINK *)mem_allocate(node->graph->heap, sizeof(struct NODELINK));
	dep->node = depnode;
	dep->next = node->firstdep;
	node->firstdep = dep;
	
	nodelinktree_insert(&node->deproot, treelink, depnode);
	
	/* create and add parent link */
	parent = (struct NODELINK *)mem_allocate(node->graph->heap, sizeof(struct NODELINK));
	parent->node = node;
	parent->next = depnode->firstparent;
	depnode->firstparent = parent;
	
	/* increase dep counter */
	node->graph->num_deps++;
		
	/* return the dependency */
	return depnode;
}


struct NODE *node_add_job_dependency_withnode(struct NODE *node, struct NODE *depnode)
{
	struct NODELINK *dep;
	struct NODETREELINK *treelink;
	
	/* make sure that the node doesn't try to depend on it self */
	if(depnode == node)
	{
		if(node->cmdline)
		{
			printf("error: node '%s' is depended on itself and is produced by a job\n", node->filename);
			return (struct NODE*)0x0;
		}
		
		return node;
	}

	/* check if we are already dependent on this node */
	treelink = nodelinktree_find_closest(node->jobdeproot, depnode->hashid);
	if(treelink != NULL && treelink->node->hashid == depnode->hashid)
		return depnode;
	
	/* create and add job dependency link */
	dep = (struct NODELINK *)mem_allocate(node->graph->heap, sizeof(struct NODELINK));
	dep->node = depnode;
	dep->next = node->firstjobdep;
	node->firstjobdep = dep;
	
	nodelinktree_insert(&node->jobdeproot, treelink, depnode);	
	
	return depnode;
}


/* adds a dependency to a node */
struct NODE *node_add_dependency(struct NODE *node, const char *filename)
{
	struct NODE *depnode = node_get(node->graph, filename);
	if(!depnode)
		return NULL;
	return node_add_dependency_withnode(node, depnode);
}

static struct NODE *node_add_constraint(struct NODELINK **first, struct NODE *node, const char *filename)
{
	struct NODE *contraint = node_get(node->graph, filename);
	struct NODELINK *link = (struct NODELINK *)mem_allocate(node->graph->heap, sizeof(struct NODELINK));
	link->node = contraint;
	link->next = *first;
	*first = link;
	return contraint;
}

struct NODE *node_add_constraint_shared(struct NODE *node, const char *filename)
{
	return node_add_constraint(&node->constraint_shared, node, filename);
}

struct NODE *node_add_constraint_exclusive(struct NODE *node, const char *filename)
{
	return node_add_constraint(&node->constraint_exclusive, node, filename);
}

/* functions to handle with bit array access */
static unsigned char *bitarray_allocate(int size)
{ return (unsigned char *)malloc((size+7)/8); }

static void bitarray_zeroall(unsigned char *a, int size)
{ memset(a, 0, (size+7)/8); }

static void bitarray_free(unsigned char *a)
{ free(a); }

static int bitarray_value(unsigned char *a, int id)
{ return a[id>>3]&(1<<(id&0x7)); }

static void bitarray_set(unsigned char *a, int id)
{ a[id>>3] |= (1<<(id&0x7)); }

static void bitarray_clear(unsigned char *a, int id)
{ a[id>>3] &= ~(1<<(id&0x7)); }

/* ************* */
static int node_walk_r(
	struct NODEWALK *walk,
	struct NODE *node)
{
	/* we should detect changes here before we run */
	struct NODELINK *dep;
	struct NODEWALKPATH path;
	int result = 0;
	int flags = walk->flags;
	
	/* check and set mark */
	if(bitarray_value(walk->mark, node->id))
		return 0; 
	bitarray_set(walk->mark, node->id);
	
	if(flags&NODEWALK_UNDONE)
	{
		if(node->workstatus != NODESTATUS_UNDONE)
			return 0;
	}

	if(flags&NODEWALK_TOPDOWN)
	{
		walk->node = node;
		result = walk->callback(walk);
	}

	/* push parent */
	path.node = node;
	path.parent = walk->parent;
	walk->parent = &path;
	walk->depth++;
	
	/* build all dependencies */
	dep = node->firstdep;
	if(flags&NODEWALK_JOBS)
		dep = node->firstjobdep;
	for(; dep; dep = dep->next)
	{
		result = node_walk_r(walk, dep->node);
		if(result)
			break;
	}

	/* pop parent */
	walk->depth--;
	walk->parent = walk->parent->parent;
	
	/* unmark the node so we can walk this tree again if needed */
	if(!(flags&NODEWALK_QUICK))
		bitarray_clear(walk->mark, node->id);
	
	/* return if we have an error */
	if(result)
		return result;

	/* check if we need to rebuild this node */
	if(!(flags&NODEWALK_FORCE) && !node->dirty)
		return 0;
	
	/* build */
	if(flags&NODEWALK_BOTTOMUP)
	{
		walk->node = node;
		result = walk->callback(walk);
	}
	
	return result;
}

/* walks through all the active nodes that needs a recheck */
static int node_walk_do_revisits(struct NODEWALK *walk)
{
	int result;
	struct NODE *node;
	
	/* no parent or depth info is available */
	walk->parent = NULL;
	walk->depth = 0;
	walk->revisiting = 1;

	while(walk->firstrevisit)
	{
		/* pop from the list */
		node = walk->firstrevisit->node;
		walk->firstrevisit->node = NULL;
		walk->firstrevisit = walk->firstrevisit->next;
		
		/* issue the call */
		walk->node = node;
		result = walk->callback(walk);
		if(result)
			return result;
	}
	
	/* return success */
	return 0;
}

int node_walk(
	struct NODE *node,
	int flags,
	int (*callback)(struct NODEWALK*),
	void *u)
{
	struct NODEWALK walk;
	int result;
	
	/* set walk parameters */
	walk.depth = 0;
	walk.flags = flags;
	walk.callback = callback;
	walk.user = u;
	walk.parent = 0;
	walk.revisiting = 0;
	walk.firstrevisit = NULL;
	walk.revisits = NULL;

	/* allocate and clear mark and sweep array */
	walk.mark = bitarray_allocate(node->graph->num_nodes);
	bitarray_zeroall(walk.mark, node->graph->num_nodes);
	
	/* allocate memory for activation */
	if(flags&NODEWALK_REVISIT)
	{
		walk.revisits = malloc(sizeof(struct NODEWALKREVISIT)*node->graph->num_nodes);
		memset(walk.revisits, 0, sizeof(struct NODEWALKREVISIT)*node->graph->num_nodes);
	}

	/* do the walk */
	result = node_walk_r(&walk, node);
	
	/* do the walk of all active elements, if we don't have an error */
	if(flags&NODEWALK_REVISIT && !result)
	{
		node_walk_do_revisits(&walk);
		free(walk.revisits);
	}

	/* free the array and return */
	bitarray_free(walk.mark);
	return result;
}

void node_walk_revisit(struct NODEWALK *walk, struct NODE *node)
{
	struct NODEWALKREVISIT *revisit = &walk->revisits[node->id];
	
	/* check if node already marked for revisit */
	if(revisit->node)
		return;
	
	/* no need to revisit the node if there is a visit to be done for it */
	/* TODO: the necessarily of this check is unknown. should check some larger builds to see
			if it helps any substantial amount. */
	if(!walk->revisiting && !bitarray_value(walk->mark, node->id))
		return;
	
	/* insert the node to the nodes to revisit */
	revisit->node = node;
	revisit->next = walk->firstrevisit;
	walk->firstrevisit = revisit;
}

void node_debug_dump(struct GRAPH *graph)
{
	struct NODE *node = graph->first;
	struct NODELINK *link;

	for(;node;node = node->next)
	{
		printf("%s\n", node->filename);
		for(link = node->firstdep; link; link = link->next)
			printf("   DEPEND %s\n", link->node->filename);
	}
}

/* dumps all nodes to the stdout */
void node_debug_dump_detailed(struct GRAPH *graph)
{
	struct NODE *node = graph->first;
	struct NODELINK *link;
	const char *tool;
	
	for(;node;node = node->next)
	{
		static const char *dirtyflag[] = {"--", "MI", "CH", "DD", "DN", "GS"};
		tool = "***";
		if(node->cmdline)
			tool = node->cmdline;
		printf("%08x %s   %s   %-15s\n", (unsigned)node->timestamp, dirtyflag[node->dirty], node->filename, tool);
		for(link = node->firstdep; link; link = link->next)
			printf("%08x %s      DEPEND %s\n", (unsigned)link->node->timestamp, dirtyflag[link->node->dirty], link->node->filename);
		for(link = node->firstparent; link; link = link->next)
			printf("%08x %s      PARENT %s\n", (unsigned)link->node->timestamp, dirtyflag[link->node->dirty], link->node->filename);
		for(link = node->constraint_shared; link; link = link->next)
			printf("%08x %s      SHARED %s\n", (unsigned)link->node->timestamp, dirtyflag[link->node->dirty], link->node->filename);
		for(link = node->constraint_exclusive; link; link = link->next)
			printf("%08x %s      EXCLUS %s\n", (unsigned)link->node->timestamp, dirtyflag[link->node->dirty], link->node->filename);
		for(link = node->firstjobdep; link; link = link->next)
			printf("%08x %s      JOBDEP %s\n", (unsigned)link->node->timestamp, dirtyflag[link->node->dirty], link->node->filename);
	}
}


static int node_debug_dump_dot_callback(struct NODEWALK *walkinfo)
{
	struct NODE *node = walkinfo->node;
	struct NODELINK *link;

	/* skip top node, always the build target */
	if(node == walkinfo->user)
		return 0;

	printf("node%d [label=\"%s\"];\n", node->id, node->filename);
	for(link = node->firstdep; link; link = link->next)
		printf("node%d -> node%d;\n", link->node->id, node->id);
	return 0;
}

/* dumps all nodes to the stdout */
void node_debug_dump_dot(struct GRAPH *graph, struct NODE *top)
{
	printf("digraph {\n");
	printf("graph [rankdir=\"LR\"];\n");
	printf("node [shape=box, height=0.25, color=gray, fontsize=8];\n");
	node_walk(top, NODEWALK_FORCE|NODEWALK_TOPDOWN|NODEWALK_QUICK, node_debug_dump_dot_callback, top);
	printf("}\n");
}

static int node_debug_dump_jobs_dot_callback(struct NODEWALK *walkinfo)
{
	struct NODE *node = walkinfo->node;
	struct NODELINK *link;

	/* skip top node, always the build target */
	if(node == walkinfo->user)
		return 0;

	printf("node%d [shape=box, label=\"%s\"];\n", node->id, node->filename);
	for(link = node->firstjobdep; link; link = link->next)
		printf("node%d -> node%d;\n", link->node->id, node->id);
	return 0;
}

void node_debug_dump_jobs_dot(struct GRAPH *graph, struct NODE *top)
{
	printf("digraph {\n");
	printf("graph [rankdir=\"LR\"];\n");
	printf("node [shape=box, height=0.25, color=gray, fontsize=8];\n");
	node_walk(top, NODEWALK_FORCE|NODEWALK_TOPDOWN|NODEWALK_JOBS|NODEWALK_QUICK, node_debug_dump_jobs_dot_callback, top);
	printf("}\n");
}

void node_debug_dump_jobs(struct GRAPH *graph)
{
	struct NODELINK *link;
	struct NODE *node = graph->first;
	static const char *dirtyflag[] = {"--", "MI", "CH", "DD", "DN", "GS"};
	printf("MI = Missing CH = Command hash dirty, DD = Dependency dirty\n");
	printf("DN = Dependency is newer, GS = Global stamp is newer\n");
	printf("Dirty Depth %-30s   Command\n", "Filename");
	for(;node;node = node->next)
	{
		if(node->cmdline)
		{
			printf(" %s    %3d  %-30s   %s\n", dirtyflag[node->dirty], node->depth, node->filename, node->cmdline);
			
			for(link = node->firstjobdep; link; link = link->next)
				printf(" %s         + %-30s\n", dirtyflag[link->node->dirty], link->node->filename);
		}
	}
}
