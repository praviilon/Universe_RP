/*
===========================================================================
GLX_U: [Shader Aim] /shaderaim, the client's half

/shaderaim is a server command (Cmd_ShaderAim_f, g_cmds.c). The server checks that the admin has the
Entities and Remaps power and then sends that one client "rpshaderaim", which lands here: the client,
which has the map and draws it, finds what is under the crosshair and prints its shader -- the name
/remap takes -- in its own console. Nothing in the engine can say which shader is where (a trace gives
no shader, on either side), so the map file itself is read: on the first use in a map, the parts of
maps/<map>.bsp that hold the shader names, the brush models and the drawn surfaces are read once and
kept until the client game shuts down (map change, vid_restart, disconnect).

What counts is what you see, not what you collide with: the drawn surfaces of the world and of the
brush entities (doors, lifts, func_static ...) where they are drawn now, curved patches as the renderer
draws them, so glass, water, decals and sky are found and invisible clip or caulk is seen through.
Model entities and the map's static models, whose triangles are not in the map, are found by their
bounds and named by their model file, with up to RP_SA_MAX_MODEL_SHADERS shader names read from it.

Every number read from a file is checked before it is used: a damaged or hostile map or model gives a
message, never a read outside what was read.
===========================================================================
*/

#include "cg_local.h"
#include "qcommon/qfiles.h"

#define RP_SA_RANGE				8192.0f		// how far the aim reaches
#define RP_SA_PATCH_STEPS		8			// each 3x3 patch block is drawn as this many steps a side
#define RP_SA_CHUNK				65536		// the map is read this much at a time
#define RP_SA_MAX_SHADERS		65536
#define RP_SA_MAX_MODELS		65536
#define RP_SA_MAX_SURFACES		(1024 * 1024)
#define RP_SA_MAX_VERTS			(4 * 1024 * 1024)
#define RP_SA_MAX_INDEXES		(16 * 1024 * 1024)
#define RP_SA_MAX_POSITIONS		(4 * 1024 * 1024)	// the map's vertices and the patches' own
#define RP_SA_MAX_TRIS			(8 * 1024 * 1024)
#define RP_SA_MAX_PATCH_SIDE	129					// a patch side, in control points (odd, 3 or more)
#define RP_SA_MAX_COORD			1048576.0f			// a vertex further out than this is a broken file
#define RP_SA_MAX_MODEL_SHADERS	8
#define RP_SA_MAX_MODEL_BYTES	(64 * 1024 * 1024)	// a model file larger than this is not read
#define RP_SA_DEBOUNCE_MSEC		1000
#define RP_SA_TIE				1.0f				// a surface wins over a model's box this close behind
#define RP_SA_G2_REACH			512.0f				// GLX_U: [Shader Aim] a Ghoul2 model is tried this near the aim, at least

typedef struct {
	int			shader;			// index in the map's shader list
	int			firstTri;		// in rpSa.tris (three position indexes each)
	int			numTris;
	vec3_t		mins, maxs;
} rpSaSurf_t;

typedef struct {
	int			firstSurface;
	int			numSurfaces;
} rpSaModel_t;

typedef struct {
	qboolean	tried;			// a read was tried in this map (kept or failed)
	qboolean	ok;
	int			numShaders;
	char		(*shaders)[MAX_QPATH];
	int			numModels;		// 0 is the world, n the brush model "*n"
	rpSaModel_t	*models;
	int			numSurfs;		// as many as the map has; one that is not drawn has no triangles
	rpSaSurf_t	*surfs;
	int			numPos;
	vec3_t		*pos;
	int			numTris;
	int			*tris;
} rpSaMap_t;

static rpSaMap_t rpSa;
static int rpSaNextAllowed;

// the path of each of the map's static models, as CG_MiscEnt registered it (the client keeps only a handle)
static char rpSaStaticPath[MAX_STATIC_MODELS][MAX_QPATH];

void RP_ShaderAimNoteStatic( int index, const char *path )
{
	if ( index >= 0 && index < MAX_STATIC_MODELS )
		Q_strncpyz( rpSaStaticPath[index], path ? path : "", sizeof( rpSaStaticPath[index] ) );
}

void RP_ShaderAimFree( void )
{
	free( rpSa.shaders );
	free( rpSa.models );
	free( rpSa.surfs );
	free( rpSa.pos );
	free( rpSa.tris );
	memset( &rpSa, 0, sizeof( rpSa ) );
}

/*
==================
Reading the map
==================
*/

typedef struct {
	int			lump;
	int			recSize;
	int			maxCount;
	int			ofs, len;
	byte		*data;
} rpSaLump_t;

static qboolean RP_SA_FiniteCoord( float v )
{
	return ( v == v && v > -RP_SA_MAX_COORD && v < RP_SA_MAX_COORD ) ? qtrue : qfalse;	// v == v: not NaN
}

static void RP_SA_AddBounds( rpSaSurf_t *s, const vec3_t p, qboolean first )
{
	if ( first )
	{
		VectorCopy( p, s->mins );
		VectorCopy( p, s->maxs );
		return;
	}
	AddPointToBounds( p, s->mins, s->maxs );
}

// quadratic Bezier weights, as the renderer's patch tessellation uses
static void RP_SA_Bernstein( float u, float w[3] )
{
	w[0] = ( 1.0f - u ) * ( 1.0f - u );
	w[1] = 2.0f * u * ( 1.0f - u );
	w[2] = u * u;
}

/*
RP_ShaderAimLoad

Reads the map file at path into rpSa. qfalse, with rpSa empty, when the file cannot be opened or any
number in it is out of range. Exposed for the probe; the client calls it through RP_SA_Ensure.
*/
qboolean RP_ShaderAimLoad( const char *path )
{
	rpSaLump_t lumps[5] = {
		{ LUMP_SHADERS,		sizeof( dshader_t ),	RP_SA_MAX_SHADERS },
		{ LUMP_MODELS,		sizeof( dmodel_t ),		RP_SA_MAX_MODELS },
		{ LUMP_DRAWVERTS,	sizeof( mapVert_t ),	RP_SA_MAX_VERTS },
		{ LUMP_DRAWINDEXES,	sizeof( int ),			RP_SA_MAX_INDEXES },
		{ LUMP_SURFACES,	sizeof( dsurface_t ),	RP_SA_MAX_SURFACES },
	};
	enum { L_SHADERS, L_MODELS, L_VERTS, L_INDEXES, L_SURFACES };
	fileHandle_t f = 0;
	dheader_t header;
	byte *chunk = NULL;
	int fileLen, end, pos, i, j;
	int numVerts, numIndexes, numSurfaces, totalPos, totalTris, nextPos, nextTri;
	const dsurface_t *dsurf;
	const mapVert_t *verts;
	const int *indexes;
	qboolean ok = qfalse;

	RP_ShaderAimFree();

	fileLen = trap->FS_Open( path, &f, FS_READ );
	if ( !f || fileLen < (int)sizeof( header ) )
		goto done;
	if ( trap->FS_Read( &header, sizeof( header ), f ) != (int)sizeof( header ) )
		goto done;
	if ( LittleLong( header.ident ) != BSP_IDENT || LittleLong( header.version ) != BSP_VERSION )
		goto done;

	// where each part is, all inside the file and a whole number of records
	end = sizeof( header );
	for ( i = 0; i < 5; i++ )
	{
		rpSaLump_t *l = &lumps[i];

		l->ofs = LittleLong( header.lumps[l->lump].fileofs );
		l->len = LittleLong( header.lumps[l->lump].filelen );
		if ( l->len < 0 || l->ofs < (int)sizeof( header ) || l->ofs > fileLen || l->len > fileLen - l->ofs )
			goto done;
		if ( l->len % l->recSize || l->len / l->recSize > l->maxCount )
			goto done;
		if ( l->len )
		{
			l->data = (byte *)malloc( l->len );
			if ( !l->data )
				goto done;
		}
		if ( l->ofs + l->len > end )
			end = l->ofs + l->len;
	}

	// there is no seek: read from the start, up to the end of the last part needed, copying each
	// part out as its bytes go by (lightmaps, light grid and visibility usually come after all of them)
	chunk = (byte *)malloc( RP_SA_CHUNK );
	if ( !chunk )
		goto done;
	for ( pos = sizeof( header ); pos < end; )
	{
		int n = end - pos > RP_SA_CHUNK ? RP_SA_CHUNK : end - pos;

		if ( trap->FS_Read( chunk, n, f ) != n )
			goto done;
		for ( i = 0; i < 5; i++ )
		{
			rpSaLump_t *l = &lumps[i];
			int from = l->ofs > pos ? l->ofs : pos;
			int to = ( l->ofs + l->len ) < ( pos + n ) ? ( l->ofs + l->len ) : ( pos + n );

			if ( from < to )
				memcpy( l->data + ( from - l->ofs ), chunk + ( from - pos ), to - from );
		}
		pos += n;
	}

	numVerts = lumps[L_VERTS].len / (int)sizeof( mapVert_t );
	numIndexes = lumps[L_INDEXES].len / (int)sizeof( int );
	numSurfaces = lumps[L_SURFACES].len / (int)sizeof( dsurface_t );
	verts = (const mapVert_t *)lumps[L_VERTS].data;
	indexes = (const int *)lumps[L_INDEXES].data;
	dsurf = (const dsurface_t *)lumps[L_SURFACES].data;

	// the shader names
	rpSa.numShaders = lumps[L_SHADERS].len / (int)sizeof( dshader_t );
	if ( rpSa.numShaders < 1 )
		goto done;
	rpSa.shaders = calloc( rpSa.numShaders, sizeof( *rpSa.shaders ) );
	if ( !rpSa.shaders )
		goto done;
	for ( i = 0; i < rpSa.numShaders; i++ )
	{
		const dshader_t *ds = (const dshader_t *)lumps[L_SHADERS].data + i;

		memcpy( rpSa.shaders[i], ds->shader, MAX_QPATH );
		rpSa.shaders[i][MAX_QPATH - 1] = '\0';
	}

	// the brush models: 0 the world, n "*n"
	rpSa.numModels = lumps[L_MODELS].len / (int)sizeof( dmodel_t );
	if ( rpSa.numModels < 1 )
		goto done;
	rpSa.models = calloc( rpSa.numModels, sizeof( *rpSa.models ) );
	if ( !rpSa.models )
		goto done;
	for ( i = 0; i < rpSa.numModels; i++ )
	{
		const dmodel_t *dm = (const dmodel_t *)lumps[L_MODELS].data + i;
		int first = LittleLong( dm->firstSurface );
		int num = LittleLong( dm->numSurfaces );

		if ( first < 0 || num < 0 || first > numSurfaces || num > numSurfaces - first )
			goto done;
		rpSa.models[i].firstSurface = first;
		rpSa.models[i].numSurfaces = num;
	}

	// the surfaces, a first pass: check each and count what it needs
	totalPos = numVerts;
	totalTris = 0;
	for ( i = 0; i < numSurfaces; i++ )
	{
		const dsurface_t *s = &dsurf[i];
		int type = LittleLong( s->surfaceType );
		int shader = LittleLong( s->shaderNum );
		int firstVert = LittleLong( s->firstVert );
		int nVerts = LittleLong( s->numVerts );

		if ( type != MST_PLANAR && type != MST_TRIANGLE_SOUP && type != MST_PATCH )
			continue;	// flares and anything else: not a surface to hit
		if ( shader < 0 || shader >= rpSa.numShaders )
			goto done;
		if ( firstVert < 0 || nVerts < 0 || firstVert > numVerts || nVerts > numVerts - firstVert )
			goto done;
		if ( type == MST_PATCH )
		{
			int w = LittleLong( s->patchWidth );
			int h = LittleLong( s->patchHeight );
			int blocks;

			if ( w < 3 || h < 3 || w > RP_SA_MAX_PATCH_SIDE || h > RP_SA_MAX_PATCH_SIDE || !( w & 1 ) || !( h & 1 ) || w * h != nVerts )
				goto done;
			blocks = ( ( w - 1 ) / 2 ) * ( ( h - 1 ) / 2 );
			if ( totalPos > RP_SA_MAX_POSITIONS - blocks * ( RP_SA_PATCH_STEPS + 1 ) * ( RP_SA_PATCH_STEPS + 1 ) )
				goto done;
			if ( totalTris > RP_SA_MAX_TRIS - blocks * RP_SA_PATCH_STEPS * RP_SA_PATCH_STEPS * 2 )
				goto done;
			totalPos += blocks * ( RP_SA_PATCH_STEPS + 1 ) * ( RP_SA_PATCH_STEPS + 1 );
			totalTris += blocks * RP_SA_PATCH_STEPS * RP_SA_PATCH_STEPS * 2;
		}
		else
		{
			int firstIndex = LittleLong( s->firstIndex );
			int nIndexes = LittleLong( s->numIndexes );

			if ( firstIndex < 0 || nIndexes < 0 || nIndexes % 3 || firstIndex > numIndexes || nIndexes > numIndexes - firstIndex )
				goto done;
			for ( j = 0; j < nIndexes; j++ )
			{
				int idx = LittleLong( indexes[firstIndex + j] );

				if ( idx < 0 || idx >= nVerts )
					goto done;
			}
			if ( totalTris > RP_SA_MAX_TRIS - nIndexes / 3 )
				goto done;
			totalTris += nIndexes / 3;
		}
	}

	rpSa.numSurfs = numSurfaces;
	rpSa.surfs = calloc( numSurfaces ? numSurfaces : 1, sizeof( *rpSa.surfs ) );
	rpSa.pos = malloc( ( totalPos ? totalPos : 1 ) * sizeof( vec3_t ) );
	rpSa.tris = malloc( ( totalTris ? totalTris : 1 ) * 3 * sizeof( int ) );
	if ( !rpSa.surfs || !rpSa.pos || !rpSa.tris )
		goto done;

	// the map's vertex positions (only those are kept)
	for ( i = 0; i < numVerts; i++ )
	{
		for ( j = 0; j < 3; j++ )
		{
			rpSa.pos[i][j] = LittleFloat( verts[i].xyz[j] );
			if ( !RP_SA_FiniteCoord( rpSa.pos[i][j] ) )
				goto done;
		}
	}

	// the second pass: each surface's triangles, a patch's made from its curve
	nextPos = numVerts;
	nextTri = 0;
	for ( i = 0; i < numSurfaces; i++ )
	{
		const dsurface_t *s = &dsurf[i];
		rpSaSurf_t *out = &rpSa.surfs[i];
		int type = LittleLong( s->surfaceType );
		int firstVert = LittleLong( s->firstVert );
		int t;

		out->shader = LittleLong( s->shaderNum );
		out->firstTri = nextTri;
		out->numTris = 0;
		if ( type == MST_PATCH )
		{
			int w = LittleLong( s->patchWidth );
			int h = LittleLong( s->patchHeight );
			int bx, by, sx, sy, r, c;

			for ( by = 0; by < ( h - 1 ) / 2; by++ )
			{
				for ( bx = 0; bx < ( w - 1 ) / 2; bx++ )
				{
					int base = nextPos;

					for ( sy = 0; sy <= RP_SA_PATCH_STEPS; sy++ )
					{
						float wy[3];

						RP_SA_Bernstein( (float)sy / RP_SA_PATCH_STEPS, wy );
						for ( sx = 0; sx <= RP_SA_PATCH_STEPS; sx++ )
						{
							float wx[3];
							float *p = rpSa.pos[nextPos++];

							RP_SA_Bernstein( (float)sx / RP_SA_PATCH_STEPS, wx );
							VectorClear( p );
							for ( r = 0; r < 3; r++ )
							{
								for ( c = 0; c < 3; c++ )
								{
									const float *cp = rpSa.pos[firstVert + ( 2 * by + r ) * w + ( 2 * bx + c )];

									VectorMA( p, wy[r] * wx[c], cp, p );
								}
							}
							RP_SA_AddBounds( out, p, ( out->numTris == 0 && bx == 0 && by == 0 && sx == 0 && sy == 0 ) ? qtrue : qfalse );
						}
					}
					for ( sy = 0; sy < RP_SA_PATCH_STEPS; sy++ )
					{
						for ( sx = 0; sx < RP_SA_PATCH_STEPS; sx++ )
						{
							int a = base + sy * ( RP_SA_PATCH_STEPS + 1 ) + sx;
							int b = a + 1;
							int d = a + ( RP_SA_PATCH_STEPS + 1 );
							int e = d + 1;
							int *tri = &rpSa.tris[nextTri * 3];

							tri[0] = a; tri[1] = b; tri[2] = e;
							tri[3] = a; tri[4] = e; tri[5] = d;
							nextTri += 2;
							out->numTris += 2;
						}
					}
				}
			}
		}
		else if ( type == MST_PLANAR || type == MST_TRIANGLE_SOUP )
		{
			int firstIndex = LittleLong( s->firstIndex );
			int nIndexes = LittleLong( s->numIndexes );

			for ( t = 0; t < nIndexes; t++ )
			{
				int v = firstVert + LittleLong( indexes[firstIndex + t] );

				rpSa.tris[nextTri * 3 + t % 3] = v;
				RP_SA_AddBounds( out, rpSa.pos[v], t == 0 ? qtrue : qfalse );
				if ( t % 3 == 2 )
				{
					nextTri++;
					out->numTris++;
				}
			}
		}
	}
	rpSa.numPos = nextPos;
	rpSa.numTris = nextTri;
	ok = ( nextPos == totalPos && nextTri == totalTris ) ? qtrue : qfalse;

done:
	if ( f )
		trap->FS_Close( f );
	free( chunk );
	for ( i = 0; i < 5; i++ )
		free( lumps[i].data );
	if ( !ok )
		RP_ShaderAimFree();
	rpSa.tried = qtrue;
	rpSa.ok = ok;
	return ok;
}

/*
==================
Ray tests
==================
*/

// where a ray from o along d (not normalised) enters the box, as a multiple of d; qfalse if it misses it
// before tmax. A ray starting inside enters at 0.
static qboolean RP_SA_RayBox( const vec3_t o, const vec3_t d, float tmax, const vec3_t mins, const vec3_t maxs, float *tEnter )
{
	float t0 = 0.0f, t1 = tmax;
	int i;

	for ( i = 0; i < 3; i++ )
	{
		if ( fabs( d[i] ) < 1e-9f )
		{
			if ( o[i] < mins[i] || o[i] > maxs[i] )
				return qfalse;
			continue;
		}
		{
			float inv = 1.0f / d[i];
			float a = ( mins[i] - o[i] ) * inv;
			float b = ( maxs[i] - o[i] ) * inv;

			if ( a > b ) { float tmp = a; a = b; b = tmp; }
			if ( a > t0 ) t0 = a;
			if ( b < t1 ) t1 = b;
			if ( t0 > t1 )
				return qfalse;
		}
	}
	if ( tEnter )
		*tEnter = t0;
	return qtrue;
}

// Moller-Trumbore, either side; the distance along d to the hit, or -1
static float RP_SA_RayTri( const vec3_t o, const vec3_t d, const vec3_t a, const vec3_t b, const vec3_t c )
{
	vec3_t e1, e2, p, q, s;
	float det, inv, u, v, t;

	VectorSubtract( b, a, e1 );
	VectorSubtract( c, a, e2 );
	CrossProduct( d, e2, p );
	det = DotProduct( e1, p );
	if ( fabs( det ) < 1e-8f )
		return -1.0f;
	inv = 1.0f / det;
	VectorSubtract( o, a, s );
	u = DotProduct( s, p ) * inv;
	if ( u < -1e-4f || u > 1.0001f )
		return -1.0f;
	CrossProduct( s, e1, q );
	v = DotProduct( d, q ) * inv;
	if ( v < -1e-4f || u + v > 1.0001f )
		return -1.0f;
	t = DotProduct( e2, q ) * inv;
	return t > 0.0f ? t : -1.0f;
}

/*
RP_ShaderAimTraceModel

The nearest drawn surface of brush model n (0 the world) along the ray o + t d, d a unit vector, with t
below *best: its shader index, *best lowered to its distance. -1 and *best untouched when none is.
Exposed for the probe.
*/
int RP_ShaderAimTraceModel( int n, const vec3_t o, const vec3_t d, float *best )
{
	const rpSaModel_t *m;
	int shader = -1;
	int i, k;

	if ( !rpSa.ok || n < 0 || n >= rpSa.numModels )
		return -1;
	m = &rpSa.models[n];
	for ( i = m->firstSurface; i < m->firstSurface + m->numSurfaces; i++ )
	{
		const rpSaSurf_t *s = &rpSa.surfs[i];
		vec3_t mins, maxs;

		if ( !s->numTris )
			continue;
		VectorSet( mins, s->mins[0] - 1, s->mins[1] - 1, s->mins[2] - 1 );
		VectorSet( maxs, s->maxs[0] + 1, s->maxs[1] + 1, s->maxs[2] + 1 );
		if ( !RP_SA_RayBox( o, d, *best, mins, maxs, NULL ) )
			continue;
		for ( k = 0; k < s->numTris; k++ )
		{
			const int *tri = &rpSa.tris[( s->firstTri + k ) * 3];
			float t = RP_SA_RayTri( o, d, rpSa.pos[tri[0]], rpSa.pos[tri[1]], rpSa.pos[tri[2]] );

			if ( t > 0.0f && t < *best )
			{
				*best = t;
				shader = s->shader;
			}
		}
	}
	return shader;
}

const char *RP_ShaderAimShaderName( int shader )
{
	return ( rpSa.ok && shader >= 0 && shader < rpSa.numShaders ) ? rpSa.shaders[shader] : "";
}

// a ray in a box turned by axis (rows, possibly scaled) about org: the box entry, as a distance along d
static qboolean RP_SA_RayOrientedBox( const vec3_t o, const vec3_t d, float tmax, const vec3_t org, matrix3_t axis,
	const vec3_t mins, const vec3_t maxs, float *t )
{
	vec3_t lo, ld, rel;
	int i;

	if ( VectorCompare( mins, vec3_origin ) && VectorCompare( maxs, vec3_origin ) )
		return qfalse;	// no bounds known (a model the renderer gives none for)
	VectorSubtract( o, org, rel );
	for ( i = 0; i < 3; i++ )
	{
		float len2 = DotProduct( axis[i], axis[i] );

		if ( len2 < 1e-12f )
			return qfalse;
		lo[i] = DotProduct( rel, axis[i] ) / len2;
		ld[i] = DotProduct( d, axis[i] ) / len2;
	}
	// a box the eye is in is not aimed at (a large model around the player would hide everything else)
	return ( RP_SA_RayBox( lo, ld, tmax, mins, maxs, t ) && *t > 0.0f ) ? qtrue : qfalse;
}

/*
==================
Model files: the shader names an md3 or glm names
==================
*/

// skips forward n bytes (there is no seek); qfalse when the file ends first
static qboolean RP_SA_Skip( fileHandle_t f, int n )
{
	char scratch[512];

	while ( n > 0 )
	{
		int k = n > (int)sizeof( scratch ) ? (int)sizeof( scratch ) : n;

		if ( trap->FS_Read( scratch, k, f ) != k )
			return qfalse;
		n -= k;
	}
	return qtrue;
}

static void RP_SA_AddName( char out[][MAX_QPATH], int *count, const char *name )
{
	int i;

	if ( !name[0] || *count >= RP_SA_MAX_MODEL_SHADERS )
		return;
	for ( i = 0; i < *count; i++ )
	{
		if ( !Q_stricmp( out[i], name ) )
			return;
	}
	Q_strncpyz( out[( *count )++], name, MAX_QPATH );
}

/*
RP_ShaderAimModelShaders

Up to RP_SA_MAX_MODEL_SHADERS distinct shader names from the md3 or glm at path, read forward only:
an md3's surfaces' shaders, a glm's surface hierarchy. -1 when the file cannot be opened or is not one
it reads; else how many names it found. Exposed for the probe.
*/
int RP_ShaderAimModelShaders( const char *path, char out[][MAX_QPATH] )
{
	fileHandle_t f = 0;
	int len, count = -1, i, j, at;
	size_t plen = path ? strlen( path ) : 0;

	if ( plen < 5 )
		return -1;
	len = trap->FS_Open( path, &f, FS_READ );
	if ( !f )
		return -1;
	if ( len <= 0 || len > RP_SA_MAX_MODEL_BYTES )
		goto done;

	if ( !Q_stricmp( path + plen - 4, ".md3" ) )
	{
		md3Header_t h;
		int numSurfaces, ofsSurfaces;

		if ( len < (int)sizeof( h ) || trap->FS_Read( &h, sizeof( h ), f ) != (int)sizeof( h ) )
			goto done;
		if ( LittleLong( h.ident ) != MD3_IDENT || LittleLong( h.version ) != MD3_VERSION )
			goto done;
		numSurfaces = LittleLong( h.numSurfaces );
		ofsSurfaces = LittleLong( h.ofsSurfaces );
		if ( numSurfaces < 0 || numSurfaces > 256 || ofsSurfaces < (int)sizeof( h ) || ofsSurfaces > len )
			goto done;
		count = 0;
		at = sizeof( h );
		if ( !RP_SA_Skip( f, ofsSurfaces - at ) )
			goto done;
		at = ofsSurfaces;
		for ( i = 0; i < numSurfaces; i++ )
		{
			md3Surface_t s;
			int start = at, numShaders, ofsShaders, ofsEnd;

			if ( len - at < (int)sizeof( s ) || trap->FS_Read( &s, sizeof( s ), f ) != (int)sizeof( s ) )
				break;
			at += sizeof( s );
			numShaders = LittleLong( s.numShaders );
			ofsShaders = LittleLong( s.ofsShaders );
			ofsEnd = LittleLong( s.ofsEnd );
			// forward only: the shaders after the surface header, the next surface after them
			if ( numShaders < 0 || numShaders > 256 || ofsShaders < (int)sizeof( s ) || ofsEnd < ofsShaders + numShaders * (int)sizeof( md3Shader_t ) || ofsEnd > len - start )
				break;
			if ( !RP_SA_Skip( f, start + ofsShaders - at ) )
				break;
			at = start + ofsShaders;
			for ( j = 0; j < numShaders; j++ )
			{
				md3Shader_t sh;

				if ( trap->FS_Read( &sh, sizeof( sh ), f ) != (int)sizeof( sh ) )
					goto done;
				at += sizeof( sh );
				sh.name[MAX_QPATH - 1] = '\0';
				RP_SA_AddName( out, &count, sh.name );
			}
			if ( !RP_SA_Skip( f, start + ofsEnd - at ) )
				break;
			at = start + ofsEnd;
		}
	}
	else if ( !Q_stricmp( path + plen - 4, ".glm" ) )
	{
		mdxmHeader_t h;
		int numSurfaces, ofsHier;

		if ( len < (int)sizeof( h ) || trap->FS_Read( &h, sizeof( h ), f ) != (int)sizeof( h ) )
			goto done;
		if ( LittleLong( h.ident ) != MDXM_IDENT || LittleLong( h.version ) != MDXM_VERSION )
			goto done;
		numSurfaces = LittleLong( h.numSurfaces );
		ofsHier = LittleLong( h.ofsSurfHierarchy );
		if ( numSurfaces < 0 || numSurfaces > 1024 || ofsHier < (int)sizeof( h ) || ofsHier > len )
			goto done;
		count = 0;
		if ( !RP_SA_Skip( f, ofsHier - (int)sizeof( h ) ) )
			goto done;
		for ( i = 0; i < numSurfaces; i++ )
		{
			// mdxmSurfHierarchy_t: name, flags, shader, shaderIndex, parentIndex, numChildren, then the children
			struct {
				char	name[MAX_QPATH];
				int		flags;
				char	shader[MAX_QPATH];
				int		shaderIndex;
				int		parentIndex;
				int		numChildren;
			} s;
			int numChildren;

			if ( trap->FS_Read( &s, sizeof( s ), f ) != (int)sizeof( s ) )
				break;
			s.shader[MAX_QPATH - 1] = '\0';
			if ( Q_stricmp( s.shader, "[nomaterial]" ) )
				RP_SA_AddName( out, &count, s.shader );
			numChildren = LittleLong( s.numChildren );
			if ( numChildren < 0 || numChildren > 1024 || !RP_SA_Skip( f, numChildren * (int)sizeof( int ) ) )
				break;
		}
	}

done:
	trap->FS_Close( f );
	return count;
}

/*
==================
The lookup
==================
*/

// a name from a file, made safe to print: control characters and colour codes out
static const char *RP_SA_Clean( const char *in )
{
	static char buf[4][MAX_QPATH];
	static int which;
	char *out = buf[which++ & 3];
	int i;

	for ( i = 0; in[i] && i < MAX_QPATH - 1; i++ )
		out[i] = ( in[i] < 32 || in[i] > 126 || in[i] == '^' ) ? '?' : in[i];
	out[i] = '\0';
	return out;
}

// what the shader is remapped to now (CS_SHADERSTATE: "old=new:time@..."), into out; qfalse when it is not
static qboolean RP_SA_RemappedTo( const char *shader, char *out, int outSize )
{
	const char *p = CG_ConfigString( CS_SHADERSTATE );

	while ( p && *p )
	{
		const char *eq = strchr( p, '=' );
		const char *colon = eq ? strchr( eq, ':' ) : NULL;
		const char *at = colon ? strchr( colon, '@' ) : NULL;

		if ( !at )
			break;
		if ( (int)strlen( shader ) == eq - p && !Q_stricmpn( p, shader, eq - p ) )
		{
			int n = colon - ( eq + 1 );

			if ( n >= outSize )
				n = outSize - 1;
			memcpy( out, eq + 1, n );
			out[n] = '\0';
			return qtrue;
		}
		p = at + 1;
	}
	return qfalse;
}

static void RP_SA_PrintShader( const char *shader, const char *where, float dist )
{
	char remap[MAX_QPATH];

	Com_Printf( "^3Shader: ^7%s ^5(%s, %i units)\n", RP_SA_Clean( shader ), where, (int)dist );
	// GLX_U: [Shader Aim] and what it is remapped to, if it is; nothing more (the /remap and /shaderlist hint is gone).
	// An entry pointing at itself (/remap X X) is no remap, as the Shader Remaps menu takes it.
	if ( RP_SA_RemappedTo( shader, remap, sizeof( remap ) ) && Q_stricmp( remap, shader ) )
		Com_Printf( "^7Remapped to: ^3%s\n", RP_SA_Clean( remap ) );
}

static void RP_SA_PrintModel( const char *path, const char *where, float dist )
{
	char names[RP_SA_MAX_MODEL_SHADERS][MAX_QPATH];
	int n, i;

	Com_Printf( "^3Model: ^7%s ^5(%s, %i units)\n", RP_SA_Clean( path ), where, (int)dist );
	n = RP_ShaderAimModelShaders( path, names );
	if ( n < 0 )
	{
		Com_Printf( "^7Its shaders could not be read.\n" );
		return;
	}
	if ( n == 0 )
	{
		Com_Printf( "^7No shaders named in the model file.\n" );
		return;
	}
	Com_Printf( "^7Its shaders%s:\n", n >= RP_SA_MAX_MODEL_SHADERS ? " (the first 8)" : "" );
	for ( i = 0; i < n; i++ )
		Com_Printf( "  ^3%s\n", RP_SA_Clean( names[i] ) );
}

// what is under the aim
typedef enum { RP_SA_NOTHING, RP_SA_SURFACE, RP_SA_PLAYER, RP_SA_MODEL, RP_SA_ENTITY } rpSaHitKind_t;

typedef struct {
	rpSaHitKind_t	kind;
	char			shader[MAX_QPATH];	// RP_SA_SURFACE: its shader
	char			where[64];			// RP_SA_SURFACE: "world" or the brush entity; RP_SA_MODEL: the entity or "static model"
	const char		*modelPath;			// RP_SA_MODEL
	int				num;				// RP_SA_PLAYER, RP_SA_ENTITY: the entity
	qboolean		body;				// GLX_U: [Shader Aim] RP_SA_PLAYER: a corpse (ET_BODY)
	float			dist;
} rpSaHit_t;

/*
GLX_U: [Shader Aim] Ghoul2 models

A .glm model has no bounds the renderer gives (R_ModelBounds reads only md3s and brush models), so a Ghoul2 prop,
player or NPC was found only by the trace, which hits only what is solid, and by its box: a non-solid
misc_model_ghoul was seen through. Each one the aim passes near is now traced against its real, animated
triangles with the engine's G2API_CollisionDetect, as CG_G2TraceCollide (cg_players.c) does for saber hits.
*/
typedef enum { RP_SA_G2_NONE, RP_SA_G2_PROP, RP_SA_G2_CHAR, RP_SA_G2_BODY } rpSaG2Kind_t;

// what a Ghoul2 entity is to the aim; NONE for one not drawn or without a Ghoul2 instance
static rpSaG2Kind_t RP_SA_Ghoul2Kind( const entityState_t *es )
{
	const centity_t *cent = &cg_entities[es->number];
	rpSaG2Kind_t kind;

	if ( es->eFlags & EF_NODRAW )
		return RP_SA_G2_NONE;
	if ( es->eType == ET_BODY )
		kind = RP_SA_G2_BODY;
	else if ( es->number < MAX_CLIENTS || es->eType == ET_PLAYER || es->eType == ET_NPC )
		kind = RP_SA_G2_CHAR;
	else if ( es->eType == ET_GENERAL && es->modelGhoul2 == 1 && es->solid != SOLID_BMODEL && es->modelindex > 0 && es->modelindex < MAX_MODELS )
		kind = RP_SA_G2_PROP;	// misc_model_ghoul and the like: drawn from the .glm in CS_MODELS (CG_General)
	else
		return RP_SA_G2_NONE;
	if ( !cent->ghoul2 || !trap->G2_HaveWeGhoul2Models( cent->ghoul2 ) )
		return RP_SA_G2_NONE;
	return kind;
}

// a ray (d of unit length) through a sphere somewhere between 0 and tmax along it
static qboolean RP_SA_RaySphere( const vec3_t o, const vec3_t d, float tmax, const vec3_t c, float r )
{
	vec3_t v;
	float tc, d2, h;

	VectorSubtract( c, o, v );
	tc = DotProduct( v, d );
	d2 = DotProduct( v, v ) - tc * tc;
	if ( d2 > r * r )
		return qfalse;
	h = sqrtf( r * r - d2 );
	return ( tc + h >= 0.0f && tc - h <= tmax ) ? qtrue : qfalse;
}

// the nearest hit of the aim on a Ghoul2 entity's triangles, as a distance along dir, or -1
static float RP_SA_Ghoul2Hit( const entityState_t *es, rpSaG2Kind_t kind, const vec3_t eye, const vec3_t dir, float tmax )
{
	centity_t *cent = &cg_entities[es->number];
	CollisionRecord_t recs[MAX_G2_COLLISIONS];
	vec3_t angles, start, end, rel;
	float radius, scale = 1.0f, best = -1.0f;
	int k;

	// only one the aim passes near is tried: within its drawn radius (g2radius, times its largest scale) or
	// RP_SA_G2_REACH of its origin, whichever is more -- a model can reach past its radius (a misc_model_ghoul
	// with no "radius" key gets its box's), and players send none
	for ( k = 0; k < 3; k++ )
	{
		if ( cent->modelScale[k] > scale )
			scale = cent->modelScale[k];
	}
	radius = (float)es->g2radius * scale;
	if ( radius < RP_SA_G2_REACH )
		radius = RP_SA_G2_REACH;
	if ( !RP_SA_RaySphere( eye, dir, tmax, cent->lerpOrigin, radius ) )
		return -1.0f;

	// turned as drawn: a prop by all its angles (CG_General), a player, NPC or corpse by its yaw (CG_G2TraceCollide)
	if ( kind == RP_SA_G2_PROP )
		VectorCopy( cent->lerpAngles, angles );
	else
		VectorSet( angles, 0.0f, cent->lerpAngles[YAW], 0.0f );
	VectorCopy( eye, start );
	VectorMA( eye, tmax, dir, end );
	memset( recs, 0, sizeof( recs ) );
	for ( k = 0; k < MAX_G2_COLLISIONS; k++ )
		recs[k].mEntityNum = -1;
	trap->G2API_CollisionDetect( recs, cent->ghoul2, angles, cent->lerpOrigin, cg.time, es->number, start, end, cent->modelScale, 0, 0, 0.0f );

	for ( k = 0; k < MAX_G2_COLLISIONS; k++ )
	{
		float t;

		if ( recs[k].mEntityNum != es->number )
			continue;
		VectorSubtract( recs[k].mCollisionPosition, eye, rel );
		t = DotProduct( rel, dir );
		if ( t > 0.0f && t <= tmax && ( best < 0.0f || t < best ) )
			best = t;
	}
	return best;
}

// the axis of a model entity, scaled as CG_General draws it: a misc_model_breakable's per-axis "modelscale_vec"
// (CG_RpVectorScale, the three scales in origin2), else iModelScale
static void RP_SA_ScaleAxis( const entityState_t *es, matrix3_t axis )
{
	float s[3];
	int k;

	if ( CG_RpVectorScale( es ) )
		VectorCopy( es->origin2, s );
	else if ( es->iModelScale > 0 )
		s[0] = s[1] = s[2] = es->iModelScale / 100.0f;
	else
		return;
	for ( k = 0; k < 3; k++ )
		VectorScale( axis[k], s[k], axis[k] );
}

/*
RP_SA_Lookup

What the crosshair is on, nearest first: a drawn surface of the world or of a brush entity, a player or NPC, a
model. Reads the map on its first use in a map.
*/
static void RP_SA_Lookup( rpSaHit_t *hit )
{
	const playerState_t *ps = &cg.predictedPlayerState;
	vec3_t eye, dir, end;
	float best = RP_SA_RANGE;
	int bestShader = -1, bestEnt = -1, bestModel = 0;	// the surface hit, and the brush entity it is on
	float modelDist = RP_SA_RANGE;
	const char *modelPath = NULL;
	char modelWhere[64];
	int playerHit = -1;
	qboolean playerBody = qfalse;	// GLX_U: [Shader Aim] playerHit is a corpse
	int traceEnt = -1;				// GLX_U: [Shader Aim] the entity the trace found, when it counts
	int i;
	trace_t tr;

	memset( hit, 0, sizeof( *hit ) );
	modelWhere[0] = '\0';

	VectorCopy( ps->origin, eye );
	eye[2] += ps->viewheight;
	AngleVectors( ps->viewangles, dir, NULL, NULL );
	VectorMA( eye, RP_SA_RANGE, dir, end );

	if ( !rpSa.tried )
		RP_ShaderAimLoad( cgs.mapname );

	// 1. the world's drawn surfaces
	bestShader = RP_ShaderAimTraceModel( 0, eye, dir, &best );

	// 2. brush entities' drawn surfaces, where they are drawn now
	for ( i = 0; i < cg.snap->numEntities; i++ )
	{
		const entityState_t *es = &cg.snap->entities[i];
		const centity_t *cent = &cg_entities[es->number];
		matrix3_t axis;
		vec3_t rel, lo, ld;
		float t = best;
		int sh;

		if ( es->solid != SOLID_BMODEL || ( es->eFlags & EF_NODRAW ) || es->modelindex < 1 || es->modelindex >= rpSa.numModels )
			continue;
		AnglesToAxis( cent->lerpAngles, axis );
		VectorSubtract( eye, cent->lerpOrigin, rel );
		lo[0] = DotProduct( rel, axis[0] ); lo[1] = DotProduct( rel, axis[1] ); lo[2] = DotProduct( rel, axis[2] );
		ld[0] = DotProduct( dir, axis[0] ); ld[1] = DotProduct( dir, axis[1] ); ld[2] = DotProduct( dir, axis[2] );
		sh = RP_ShaderAimTraceModel( es->modelindex, lo, ld, &t );
		if ( sh >= 0 && t < best )
		{
			best = t;
			bestShader = sh;
			bestEnt = es->number;
			bestModel = es->modelindex;
		}
	}

	// 3. model entities: a solid one, a player or an NPC by the trace, any other by its model's bounds
	CG_Trace( &tr, eye, vec3_origin, vec3_origin, end, ps->clientNum, MASK_SHOT );
	// GLX_U: [Shader Aim] a Ghoul2 one is left to its triangles below, not its box
	if ( tr.fraction < 1.0f && tr.entityNum < ENTITYNUM_WORLD && RP_SA_Ghoul2Kind( &cg_entities[tr.entityNum].currentState ) == RP_SA_G2_NONE )
		traceEnt = tr.entityNum;
	if ( traceEnt >= 0 && cg_entities[traceEnt].currentState.solid != SOLID_BMODEL )
	{
		const entityState_t *es = &cg_entities[tr.entityNum].currentState;

		if ( tr.entityNum < MAX_CLIENTS || es->eType == ET_PLAYER || es->eType == ET_NPC )
			playerHit = tr.entityNum;
		else if ( es->eType == ET_BODY )
		{
			playerHit = tr.entityNum;
			playerBody = qtrue;
		}
		modelDist = tr.fraction * RP_SA_RANGE;
	}
	for ( i = 0; i < cg.snap->numEntities; i++ )
	{
		const entityState_t *es = &cg.snap->entities[i];
		const centity_t *cent = &cg_entities[es->number];
		qhandle_t h;
		vec3_t mins, maxs;
		matrix3_t axis;
		float t;

		if ( ( es->eFlags & EF_NODRAW ) || es->number < MAX_CLIENTS )
			continue;

		// GLX_U: [Shader Aim] a mover's second model (a func_* "model2" key), drawn as CG_Mover does: at the
		// mover's place and angles, scaled by iModelScale -- its brushes are looked at above
		if ( es->eType == ET_MOVER && es->modelindex2 > 0 && es->modelindex2 < MAX_MODELS && ( h = cgs.gameModels[es->modelindex2] ) != 0 )
		{
			trap->R_ModelBounds( h, mins, maxs );
			AnglesToAxis( cent->lerpAngles, axis );
			if ( es->iModelScale > 0 )
			{
				VectorScale( axis[0], es->iModelScale / 100.0f, axis[0] );
				VectorScale( axis[1], es->iModelScale / 100.0f, axis[1] );
				VectorScale( axis[2], es->iModelScale / 100.0f, axis[2] );
			}
			if ( RP_SA_RayOrientedBox( eye, dir, modelDist, cent->lerpOrigin, axis, mins, maxs, &t ) && t <= modelDist )
			{
				modelDist = t;
				modelPath = CG_ConfigString( CS_MODELS + es->modelindex2 );
				playerHit = -1;
				Com_sprintf( modelWhere, sizeof( modelWhere ), "#%i, model2", es->number );
			}
		}

		// the types drawn from their modelindex: an item's model, or the model in CS_MODELS
		if ( es->solid == SOLID_BMODEL )
			continue;
		if ( RP_SA_Ghoul2Kind( es ) != RP_SA_G2_NONE )
			continue;	// GLX_U: [Shader Aim] by its triangles, below
		if ( es->eType == ET_ITEM )
		{
			if ( es->modelindex < 1 || es->modelindex >= bg_numItems )
				continue;
			h = cg_items[es->modelindex].models[0];
		}
		else if ( es->eType == ET_GENERAL || es->eType == ET_MOVER )
		{
			if ( es->modelindex < 1 || es->modelindex >= MAX_MODELS )
				continue;
			h = cgs.gameModels[es->modelindex];
		}
		else
			continue;
		if ( h )
			trap->R_ModelBounds( h, mins, maxs );
		else
		{
			VectorClear( mins );	// a ghoul2 model: no handle here, found only by the trace
			VectorClear( maxs );
		}
		AnglesToAxis( cent->lerpAngles, axis );
		RP_SA_ScaleAxis( es, axis );	// GLX_U: [Shader Aim] and a breakable's per-axis scale
		if ( !RP_SA_RayOrientedBox( eye, dir, modelDist, cent->lerpOrigin, axis, mins, maxs, &t ) )
		{
			// a solid model the trace found but the renderer gives no bounds for
			if ( !( tr.fraction < 1.0f && tr.entityNum == es->number ) )
				continue;
			t = modelDist;
		}
		if ( t <= modelDist )
		{
			const char *path = ( es->eType == ET_ITEM ) ? bg_itemlist[es->modelindex].world_model[0] : CG_ConfigString( CS_MODELS + es->modelindex );

			modelDist = t;
			modelPath = path;
			playerHit = -1;
			Com_sprintf( modelWhere, sizeof( modelWhere ), "#%i", es->number );
		}
	}

	// GLX_U: [Shader Aim] Ghoul2 props, players, NPCs and corpses, by their triangles; one behind the nearest
	// surface or model so far is not tried
	for ( i = 0; i < cg.snap->numEntities; i++ )
	{
		const entityState_t *es = &cg.snap->entities[i];
		rpSaG2Kind_t kind = RP_SA_Ghoul2Kind( es );
		float t;

		if ( kind == RP_SA_G2_NONE )
			continue;
		t = RP_SA_Ghoul2Hit( es, kind, eye, dir, ( best < modelDist ? best + RP_SA_TIE : modelDist ) );
		if ( t < 0.0f || t > modelDist )
			continue;
		modelDist = t;
		if ( kind == RP_SA_G2_PROP )
		{
			modelPath = CG_ConfigString( CS_MODELS + es->modelindex );
			playerHit = -1;
			Com_sprintf( modelWhere, sizeof( modelWhere ), "#%i", es->number );
		}
		else
		{
			playerHit = es->number;
			playerBody = ( kind == RP_SA_G2_BODY ) ? qtrue : qfalse;
		}
	}

	// 4. the map's static models, by their bounds, scaled and turned as drawn
	for ( i = 0; i < cgs.numMiscStaticModels; i++ )
	{
		const cg_staticmodel_t *sm = &cgs.miscStaticModels[i];
		vec3_t mins, maxs;
		matrix3_t axis;
		float t;

		if ( !sm->model )
			continue;
		trap->R_ModelBounds( sm->model, mins, maxs );
		memcpy( axis, sm->axes, sizeof( axis ) );
		if ( RP_SA_RayOrientedBox( eye, dir, modelDist, sm->org, axis, mins, maxs, &t ) && t < modelDist )
		{
			modelDist = t;
			modelPath = rpSaStaticPath[i];
			playerHit = -1;
			Q_strncpyz( modelWhere, "static model", sizeof( modelWhere ) );
		}
	}

	// the nearest wins; a surface over a model's box just in front of it (the box is bigger than the model)
	if ( bestShader >= 0 && best <= modelDist + RP_SA_TIE )
	{
		hit->kind = RP_SA_SURFACE;
		Q_strncpyz( hit->shader, RP_ShaderAimShaderName( bestShader ), sizeof( hit->shader ) );
		if ( bestEnt >= 0 )
			Com_sprintf( hit->where, sizeof( hit->where ), "#%i, brush model *%i", bestEnt, bestModel );
		else
			Q_strncpyz( hit->where, "world", sizeof( hit->where ) );
		hit->dist = best;
	}
	else if ( playerHit >= 0 )
	{
		hit->kind = RP_SA_PLAYER;
		hit->num = playerHit;
		hit->body = playerBody;
		hit->dist = modelDist;
	}
	else if ( modelPath )
	{
		hit->kind = RP_SA_MODEL;
		hit->modelPath = modelPath;
		Q_strncpyz( hit->where, modelWhere, sizeof( hit->where ) );
		hit->dist = modelDist;
	}
	else if ( traceEnt >= 0 )	// GLX_U: [Shader Aim] the trace's entity, unless it was a Ghoul2 one's box
	{
		hit->kind = RP_SA_ENTITY;
		hit->num = traceEnt;
		hit->dist = tr.fraction * RP_SA_RANGE;
	}
}

/*
==================
RP_ShaderAim_f

"rpshaderaim", sent by the server after it has checked the power (Cmd_ShaderAim_f, g_cmds.c).
==================
*/
void RP_ShaderAim_f( void )
{
	rpSaHit_t hit;

	if ( !cg.snap )
		return;
	if ( cg.time < rpSaNextAllowed && rpSaNextAllowed - cg.time <= RP_SA_DEBOUNCE_MSEC )
		return;
	rpSaNextAllowed = cg.time + RP_SA_DEBOUNCE_MSEC;

	if ( !rpSa.tried )
	{
		Com_Printf( "^7Reading %s's surfaces...\n", RP_SA_Clean( cgs.mapname ) );
		RP_ShaderAimLoad( cgs.mapname );
	}
	if ( !rpSa.ok )
		Com_Printf( "^1Could not read this map's surfaces. ^7Models are still found.\n" );

	RP_SA_Lookup( &hit );

	switch ( hit.kind )
	{
	case RP_SA_SURFACE:
		RP_SA_PrintShader( hit.shader, hit.where, hit.dist );
		break;
	case RP_SA_PLAYER:
		Com_Printf( "^3Player/NPC %s#%i ^5(%i units)^7: its textures come from its model and skin.\n", hit.body ? "body " : "", hit.num, (int)hit.dist );
		break;
	case RP_SA_MODEL:
		RP_SA_PrintModel( hit.modelPath, hit.where, hit.dist );
		break;
	case RP_SA_ENTITY:
		Com_Printf( "^3Entity #%i ^5(%i units)^7: no model to name.\n", hit.num, (int)hit.dist );
		break;
	default:
		Com_Printf( "^7Nothing under your aim.\n" );
		break;
	}
}

/*
RP_ShaderAimPick

GLX_U: [Shader Remaps Menu] the menu's "Pick from world" (cg_rpextras.c): the shader of the map surface or brush
entity under the aim into out, with what it is now remapped to in remap ("" for nothing). qfalse, with the
reason in why, when the aim is on no drawn surface.
*/
qboolean RP_ShaderAimPick( char *out, int outSize, char *remap, int remapSize, char *why, int whySize )
{
	rpSaHit_t hit;

	out[0] = remap[0] = why[0] = '\0';
	if ( !cg.snap )
	{
		Q_strncpyz( why, "Not in the game yet.", whySize );
		return qfalse;
	}
	RP_SA_Lookup( &hit );
	switch ( hit.kind )
	{
	case RP_SA_SURFACE:
		Q_strncpyz( out, hit.shader, outSize );
		if ( RP_SA_RemappedTo( hit.shader, remap, remapSize ) && !Q_stricmp( remap, hit.shader ) )
			remap[0] = '\0';	// an entry pointing at itself (/remap X X) is no remap
		return qtrue;
	case RP_SA_PLAYER:
		Q_strncpyz( why, hit.body ? "That is a body: its textures come from its model and skin." : "That is a player or an NPC: its textures come from its model and skin.", whySize );
		break;
	case RP_SA_MODEL:
		Q_strncpyz( why, "That is a model, not a map surface: /shaderaim names its shaders.", whySize );
		break;
	case RP_SA_ENTITY:
		Q_strncpyz( why, "That entity has no map surface under your aim.", whySize );
		break;
	default:
		Q_strncpyz( why, rpSa.ok ? "No map surface under your aim." : "Could not read this map's surfaces.", whySize );
		break;
	}
	return qfalse;
}
