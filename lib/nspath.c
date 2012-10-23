#define _GNU_SOURCE 1
#include <unistd.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <stdio.h>
#include "nspath.h"

#ifndef PATH_MAX
# define PATH_MAX 4096
#endif

#define PCOMP_IGNORE (1 << 0) /* marks negated or ignored components */
#define PCOMP_ALLOC  (1 << 1) /* future used for symlink resolutions */

/* path component struct */
struct pcomp {
	char *str; /* file- or directoryname */
	unsigned int len;   /* length of this component */
	int flags;
};

static inline int path_components(const char *path)
{
	char *slash;
	int comps = 1;
	if (!path)
		return 0;
	for (slash = strchr(path, '/'); slash; slash = strchr(slash + 1, '/'))
		comps++;
	return comps;
}

static char *pcomp_construct(struct pcomp *pcomp, int comps)
{
	int i, plen = 0, offset = 0;
	char *path;

	for (i = 0; i < comps; i++) {
		if(pcomp[i].flags & PCOMP_IGNORE)
			continue;
		plen += pcomp[i].len + 1;
	}

	path = malloc(plen + 2);

	for (i = 0; i < comps; i++) {
		if(pcomp[i].flags & PCOMP_IGNORE)
			continue;
		memcpy(path + offset, pcomp[i].str, pcomp[i].len);
		offset += pcomp[i].len;
		if (i < comps - 1)
			path[offset++] = '/';
	}
	path[offset] = 0;

	return path;
}

/*
 * Converts "foo/bar/.././lala.txt" to "foo/lala.txt".
 * "../../../../../bar/../foo/" becomes "/foo/"
 */
char *nspath_normalize(const char *orig_path)
{
	struct pcomp pcomp[256]; /* >256 components will fail hard */
	int comps, i = 0, m, depth = 0;
	char *path, *rpath, *p, *slash;

	if (!orig_path || !*orig_path)
		return NULL;

	rpath = strdup(orig_path);
	comps = path_components(rpath);
	memset(pcomp, 0, sizeof(pcomp));
	p = pcomp[0].str = rpath;
	for (; p; p = slash, i++) {
		slash = strchr(p, '/');
		if (slash) {
			*slash = 0;
			slash++;
		}
		pcomp[i].len = strlen(p);
		pcomp[i].str = p;

		if (*p == '.') {
			if (p[1] == 0) {
				/* dot-slash is always just ignored */
				pcomp[i].flags |= PCOMP_IGNORE;
				continue;
			}
			if ((*orig_path == '/' || depth) && p[1] == '.' && p[2] == 0) {
				/* dot-dot-slash negates the previous non-negated component */
				pcomp[i].flags |= PCOMP_IGNORE;
				for(m = 0; depth && m <= i; m++) {
					if (pcomp[i - m].flags & PCOMP_IGNORE) {
						continue;
					}
					pcomp[i - m].flags |= PCOMP_IGNORE;
					depth--;
					break; /* we only remove one level at most */
				}
				continue;
			}
		}
		/* convert multiple slashes to one */
		if (i && !*p) {
			pcomp[i].flags |= PCOMP_IGNORE;
			continue;
		}
		depth++;
	}

	path = pcomp_construct(pcomp, comps);
	free(rpath);
	return path;
}

char *nspath_absolute(const char *rel_path, const char *base)
{
	char cwd[4096];
	int len;
	char *path = NULL, *normpath;

	if (*rel_path == '/')
		return nspath_normalize(rel_path);

	if (!base) {
		if (getcwd(cwd, sizeof(cwd) - 1) < 0)
			return NULL;
		base = cwd;
	}

	len = asprintf(&path, "%s/%s", base, rel_path);
	if (len <= 0) {
		if (path)
			free(path);
		return NULL;
	}

	normpath = nspath_normalize(path);
	free(path);

	return normpath;
}

char *nspath_real(const char *rel_path, const char *base)
{
	char *abspath, *ret;

	abspath = nspath_absolute(rel_path, base);
	ret = realpath(abspath, NULL);
	if(abspath)
		free(abspath);
	return ret;
}