// SPDX-License-Identifier: GPL-3.0-or-later
/*
Decals: bullet holes, scorch marks, blood on the world and on static models, the game's
cheap decals, and the deploy footprint (a "logged decal" model the game fills).

The projection and the clipping are Doom 3's (src/renderer/ModelDecal.cpp, Doom 3 GPL
source, id Software): a winding with texture coordinates and a projection origin give a
volume of six planes; every triangle of a target model inside it is clipped to the volume,
textured by the winding's texture axes and faded by its depth. ModelDecal.cpp itself is
written against the Doom 3 renderer types (tr_local.h), which the live renderer
(draw_local.h) does not use, so its logic is carried here instead.

Each target keeps its decals as small meshes, in the target's own space: the world (all
static world models, in world space) and each render entity with a static model (its
local space; animated models are left out, as Doom 3 left them to overlays). Every view
adds, for each visible target, one surface per material, built in per-view memory as
the imposter cards are (tr_light.cpp), and fades the colors with time: a material decal
over its material's decalInfo, a cheap decal over its declaration's lifetime.
*/

#include "../idlib/precompiled.h"
#pragma hdrstop

#include "draw_local.h"
#include "VertexCache.h"
#include "../decllib/declDecal.h"

#include <unordered_map>

// the live renderer's (tr_light.cpp)
drawSurf_s* R_AddDrawSurf( const srfTriangles_t* triangles, const viewEntity_s* space,
	const renderEntity_t* renderEntity, const idMaterial* material,
	const idScreenRect& scissor, int surfID, const float* reusedMaterialRegisters );

idCVar r_skipDecals( "r_skipDecals", "0", CVAR_RENDERER | CVAR_BOOL, "draw no decals (bullet holes, scorch marks, blood)" );
idCVar r_maxWorldDecals( "r_maxWorldDecals", "512", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER, "the most decals kept on the world; the oldest goes first", 0, 4096 );
idCVar r_maxEntityDecals( "r_maxEntityDecals", "32", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER, "the most decals kept on one model; the oldest goes first", 0, 256 );

namespace {

const int DECAL_PLANES		= 6;		// the winding's four edges, its plane and the back plane
const int MAX_MESH_VERTS	= 1024;		// one decal's triangles, after clipping

// Doom 3's decalProjectionInfo_t
struct decalProjection_t {
	idVec3		origin;
	idBounds	bounds;
	idPlane		boundingPlanes[ DECAL_PLANES ];	// positive sides face out
	idPlane		fadePlanes[ 2 ];
	idPlane		textureAxis[ 2 ];
	bool		parallel;
	float		fadeDepth;
};

enum decalFade_t {
	DF_MATERIAL,	// the material's decalInfo: stay, then fade from start to end
	DF_LIFETIME,	// a cheap decal: start to end color over its lifetime, then gone
	DF_NONE			// a logged decal: its color, until the game resets it
};

struct decalMesh_t {
	const idMaterial*		material;
	int						startTime;
	decalFade_t				fade;
	int						lifeTime;	// DF_LIFETIME, msec
	idVec4					startColor;
	idVec4					endColor;
	idList< idDrawVert >	verts;
	idList< float >			depthFade;
	idList< glIndex_t >		indexes;
};

struct decalTarget_t {
	const idRenderModel*	model;		// the model the meshes were cut from; another model drops them
	idList< decalMesh_t* >	meshes;
	decalTarget_t() : model( NULL ) {}
	~decalTarget_t() { meshes.DeleteContents( true ); }
};

// per view: the surfaces built for it, freed with the view (R_FreeBuiltDrawView)
struct frameGeometry_t {
	srfTriangles_t*	tri;
	idDrawVert*		verts;
	glIndex_t*		indexes;
};

decalTarget_t								worldDecals;
std::unordered_map< int, decalTarget_t* >	entityDecals;
std::unordered_map< const idRenderModel*, decalMesh_t* > loggedDecals;	// CreateDecalModel's models
idList< frameGeometry_t >					frameGeometry;

/*
Doom 3's idRenderModelDecal::CreateProjectionInfo: the volume, the fade planes and the
texture axes for a winding of four points with texture coordinates.
*/
bool CreateProjection( decalProjection_t& info, const idFixedWinding& winding, const idVec3& projectionOrigin, bool parallel, float fadeDepth ) {
	if ( winding.GetNumPoints() != DECAL_PLANES - 2 ) {
		return false;
	}
	info.origin = projectionOrigin;
	info.parallel = parallel;
	info.fadeDepth = fadeDepth > 0.0f ? fadeDepth : 1.0f;

	idPlane windingPlane;
	winding.GetPlane( windingPlane );
	const float depth = windingPlane.Distance( projectionOrigin );

	winding.GetBounds( info.bounds );
	if ( parallel ) {
		info.bounds.ExpandSelf( depth );
	} else {
		info.bounds.AddPoint( projectionOrigin );
	}

	if ( parallel ) {
		for ( int i = 0; i < winding.GetNumPoints(); i++ ) {
			const idVec3 edge = winding[ ( i + 1 ) % winding.GetNumPoints() ].ToVec3() - winding[ i ].ToVec3();
			info.boundingPlanes[ i ].Normal().Cross( windingPlane.Normal(), edge );
			info.boundingPlanes[ i ].Normalize();
			info.boundingPlanes[ i ].FitThroughPoint( winding[ i ].ToVec3() );
		}
	} else {
		for ( int i = 0; i < winding.GetNumPoints(); i++ ) {
			info.boundingPlanes[ i ].FromPoints( projectionOrigin, winding[ i ].ToVec3(), winding[ ( i + 1 ) % winding.GetNumPoints() ].ToVec3() );
		}
	}
	info.boundingPlanes[ DECAL_PLANES - 2 ] = windingPlane;
	info.boundingPlanes[ DECAL_PLANES - 2 ][ 3 ] -= depth;
	info.boundingPlanes[ DECAL_PLANES - 1 ] = -windingPlane;

	info.fadePlanes[ 0 ] = windingPlane;
	info.fadePlanes[ 0 ][ 3 ] -= info.fadeDepth;
	info.fadePlanes[ 1 ] = -windingPlane;
	info.fadePlanes[ 1 ][ 3 ] += depth - info.fadeDepth;

	const idVec5& a = winding[ 0 ];
	const idVec5& b = winding[ 1 ];
	const idVec5& c = winding[ 2 ];
	idVec5 d0, d1;
	d0 = b.ToVec3() - a.ToVec3();
	d0.s = b.s - a.s;
	d0.t = b.t - a.t;
	d1 = c.ToVec3() - a.ToVec3();
	d1.s = c.s - a.s;
	d1.t = c.t - a.t;
	const float texArea = ( d0[ 3 ] * d1[ 4 ] ) - ( d0[ 4 ] * d1[ 3 ] );
	if ( idMath::Fabs( texArea ) < 1e-8f ) {
		return false;
	}
	const float inva = 1.0f / texArea;
	idVec3 temp;
	temp[ 0 ] = ( d0[ 0 ] * d1[ 4 ] - d0[ 4 ] * d1[ 0 ] ) * inva;
	temp[ 1 ] = ( d0[ 1 ] * d1[ 4 ] - d0[ 4 ] * d1[ 1 ] ) * inva;
	temp[ 2 ] = ( d0[ 2 ] * d1[ 4 ] - d0[ 4 ] * d1[ 2 ] ) * inva;
	float len = temp.Normalize();
	if ( len <= 0.0f ) {
		return false;
	}
	info.textureAxis[ 0 ].Normal() = temp * ( 1.0f / len );
	info.textureAxis[ 0 ][ 3 ] = winding[ 0 ].s - ( winding[ 0 ].ToVec3() * info.textureAxis[ 0 ].Normal() );

	temp[ 0 ] = ( d0[ 3 ] * d1[ 0 ] - d0[ 0 ] * d1[ 3 ] ) * inva;
	temp[ 1 ] = ( d0[ 3 ] * d1[ 1 ] - d0[ 1 ] * d1[ 3 ] ) * inva;
	temp[ 2 ] = ( d0[ 3 ] * d1[ 2 ] - d0[ 2 ] * d1[ 3 ] ) * inva;
	len = temp.Normalize();
	if ( len <= 0.0f ) {
		return false;
	}
	info.textureAxis[ 1 ].Normal() = temp * ( 1.0f / len );
	info.textureAxis[ 1 ][ 3 ] = winding[ 0 ].t - ( winding[ 0 ].ToVec3() * info.textureAxis[ 1 ].Normal() );
	return true;
}

// a world-space plane in a model's space (world = local * axis + origin)
idPlane PlaneToLocal( const idPlane& plane, const idVec3& origin, const idMat3& axis ) {
	idPlane local;
	local.Normal() = axis * plane.Normal();
	local[ 3 ] = plane[ 3 ] + plane.Normal() * origin;
	return local;
}

void ProjectionToLocal( decalProjection_t& local, const decalProjection_t& info, const idVec3& origin, const idMat3& axis ) {
	local = info;
	for ( int i = 0; i < DECAL_PLANES; i++ ) {
		local.boundingPlanes[ i ] = PlaneToLocal( info.boundingPlanes[ i ], origin, axis );
	}
	for ( int i = 0; i < 2; i++ ) {
		local.fadePlanes[ i ] = PlaneToLocal( info.fadePlanes[ i ], origin, axis );
		local.textureAxis[ i ] = PlaneToLocal( info.textureAxis[ i ], origin, axis );
	}
	local.origin = ( info.origin - origin ) * axis.Transpose();
	local.bounds = info.bounds;
	local.bounds.TranslateSelf( -origin );
	local.bounds.RotateSelf( axis.Transpose() );
}

void AddWinding( decalMesh_t& mesh, const idWinding& w, const decalProjection_t& info, const idVec3& normal ) {
	if ( w.GetNumPoints() < 3 || mesh.verts.Num() + w.GetNumPoints() > MAX_MESH_VERTS ) {
		return;
	}
	const int first = mesh.verts.Num();
	const float invFadeDepth = -1.0f / info.fadeDepth;
	for ( int i = 0; i < w.GetNumPoints(); i++ ) {
		float fade = info.fadePlanes[ 0 ].Distance( w[ i ].ToVec3() ) * invFadeDepth;
		if ( fade < 0.0f ) {
			fade = info.fadePlanes[ 1 ].Distance( w[ i ].ToVec3() ) * invFadeDepth;
		}
		fade = 1.0f - idMath::ClampFloat( 0.0f, 1.0f, fade > 0.99f ? 1.0f : fade );
		idDrawVert& v = mesh.verts.Alloc();
		v.Clear();
		v.xyz = w[ i ].ToVec3();
		v.SetST( w[ i ].s, w[ i ].t );
		v.SetNormal( normal );
		v.SetTangent( info.textureAxis[ 0 ].Normal() );
		v.SetBiTangent( info.textureAxis[ 1 ].Normal() );
		v.SetColor( 0xffffffff );
		mesh.depthFade.Append( fade );
	}
	for ( int i = 2; i < w.GetNumPoints(); i++ ) {
		mesh.indexes.Append( static_cast< glIndex_t >( first ) );
		mesh.indexes.Append( static_cast< glIndex_t >( first + i - 1 ) );
		mesh.indexes.Append( static_cast< glIndex_t >( first + i ) );
	}
}

// Doom 3's AddDepthFadedWinding: split at the fade planes, so the fade runs along edges
void AddDepthFadedWinding( decalMesh_t& mesh, const idWinding& w, const decalProjection_t& info, const idVec3& normal ) {
	idFixedWinding work;
	work = w;
	for ( int i = 0; i < 2; i++ ) {
		idFixedWinding front, back;
		if ( work.Split( info.fadePlanes[ i ], 0.1f, front, back ) == SIDE_CROSS ) {
			AddWinding( mesh, back, info, normal );
			work = front;
		}
	}
	AddWinding( mesh, work, info, normal );
}

bool MaterialListed( const idMaterial* material, const idMaterial** onlyMaterials, int numOnlyMaterials ) {
	for ( int i = 0; i < numOnlyMaterials; i++ ) {
		if ( onlyMaterials[ i ] == material ) {
			return true;
		}
	}
	return false;
}

/*
Doom 3's idRenderModelDecal::CreateDecal: every triangle of the model inside the
projection, clipped to it. With onlyMaterials, only surfaces with one of those materials
take it (the deploy footprint, on the terrain); else only surfaces that allow overlays.
*/
void CutModel( decalMesh_t& mesh, const idRenderModel* model, const decalProjection_t& info, const idMaterial** onlyMaterials, int numOnlyMaterials ) {
	for ( int surfNum = 0; surfNum < model->NumSurfaces(); surfNum++ ) {
		const modelSurface_t* surf = model->Surface( surfNum );
		if ( surf == NULL || surf->geometry == NULL || surf->material == NULL ) {
			continue;
		}
		if ( onlyMaterials != NULL && numOnlyMaterials > 0 ) {
			if ( !MaterialListed( surf->material, onlyMaterials, numOnlyMaterials ) ) {
				continue;
			}
		} else if ( !surf->material->AllowOverlays() ) {
			continue;
		}
		const srfTriangles_t* stri = surf->geometry;
		if ( stri->verts == NULL || stri->indexes == NULL || stri->numVerts <= 0 || !info.bounds.IntersectsBounds( stri->bounds ) ) {
			continue;
		}
		idList< byte > cullBits;
		cullBits.SetNum( stri->numVerts );
		SIMDProcessor->DecalPointCull( cullBits.Begin(), info.boundingPlanes, stri->verts, stri->numVerts );
		for ( int index = 0; index + 2 < stri->numIndexes; index += 3 ) {
			const int v1 = stri->indexes[ index + 0 ];
			const int v2 = stri->indexes[ index + 1 ];
			const int v3 = stri->indexes[ index + 2 ];
			if ( v1 >= stri->numVerts || v2 >= stri->numVerts || v3 >= stri->numVerts ) {
				continue;
			}
			if ( cullBits[ v1 ] & cullBits[ v2 ] & cullBits[ v3 ] ) {
				continue;	// all off one side
			}
			// back-facing to the projection
			idVec3 normal = ( stri->verts[ v2 ].xyz - stri->verts[ v1 ].xyz ).Cross( stri->verts[ v3 ].xyz - stri->verts[ v1 ].xyz );
			if ( normal.Normalize() <= 0.0f ) {
				continue;
			}
			if ( normal * info.boundingPlanes[ DECAL_PLANES - 2 ].Normal() < -0.1f ) {
				continue;
			}
			idFixedWinding fw;
			fw.SetNumPoints( 3 );
			for ( int j = 0; j < 3; j++ ) {
				fw[ j ] = stri->verts[ stri->indexes[ index + j ] ].xyz;
				idVec3 point = fw[ j ].ToVec3();
				if ( !info.parallel ) {
					idVec3 dir = point - info.origin;
					float scale = 0.0f;
					info.boundingPlanes[ DECAL_PLANES - 1 ].RayIntersection( point, dir, scale );
					point = point + scale * dir;
				}
				fw[ j ].s = info.textureAxis[ 0 ].Distance( point );
				fw[ j ].t = info.textureAxis[ 1 ].Distance( point );
			}
			const int orBits = cullBits[ v1 ] | cullBits[ v2 ] | cullBits[ v3 ];
			for ( int j = 0; j < DECAL_PLANES; j++ ) {
				if ( ( orBits & ( 1 << j ) ) && !fw.ClipInPlace( -info.boundingPlanes[ j ] ) ) {
					break;
				}
			}
			if ( fw.GetNumPoints() == 0 ) {
				continue;
			}
			AddDepthFadedWinding( mesh, fw, info, normal );
			if ( mesh.verts.Num() >= MAX_MESH_VERTS ) {
				return;
			}
		}
	}
}

void AddMesh( decalTarget_t& target, decalMesh_t* mesh, int maxMeshes ) {
	if ( mesh->indexes.Num() == 0 || maxMeshes <= 0 ) {
		delete mesh;
		return;
	}
	while ( target.meshes.Num() >= maxMeshes ) {
		delete target.meshes[ 0 ];
		target.meshes.RemoveIndex( 0 );
	}
	target.meshes.Append( mesh );
}

decalMesh_t* NewMesh( const idMaterial* material, decalFade_t fade, int startTime ) {
	decalMesh_t* mesh = new decalMesh_t;
	mesh->material = material;
	mesh->startTime = startTime;
	mesh->fade = fade;
	mesh->lifeTime = 0;
	mesh->startColor.Set( 1.0f, 1.0f, 1.0f, 1.0f );
	mesh->endColor.Set( 1.0f, 1.0f, 1.0f, 1.0f );
	if ( fade == DF_MATERIAL && material != NULL ) {
		const decalInfo_t info = material->GetDecalInfo();
		mesh->startColor = info.start;
		mesh->endColor = info.end;
	}
	return mesh;
}

bool EntityTakesDecals( const renderEntity_t* entity ) {
	return entity != NULL && entity->hModel != NULL && entity->hModel->IsDynamicModel() == DM_STATIC && entity->numInsts <= 0;
}

// the mesh cut from one entity's model, for a projection in world space
void ProjectOntoEntity( idRenderWorldLocal* world, int handle, const decalProjection_t& info, const idMaterial* material, int startTime, decalFade_t fade, const idVec4* colors ) {
	renderEntity_t* entity = world->GetRenderEntity( handle );
	if ( !EntityTakesDecals( entity ) ) {
		return;
	}
	idBounds entityBounds;
	entityBounds.FromTransformedBounds( entity->hModel->Bounds( entity ), entity->origin, entity->axis );
	if ( !entityBounds.IntersectsBounds( info.bounds ) ) {
		return;
	}
	decalProjection_t local;
	ProjectionToLocal( local, info, entity->origin, entity->axis );
	decalMesh_t* mesh = NewMesh( material, fade, startTime );
	if ( colors != NULL ) {
		mesh->startColor = colors[ 0 ];
		mesh->endColor = colors[ 1 ];
		mesh->lifeTime = static_cast< int >( colors[ 2 ].x );
	}
	CutModel( *mesh, entity->hModel, local, NULL, 0 );
	decalTarget_t*& target = entityDecals[ handle ];
	if ( target == NULL ) {
		target = new decalTarget_t;
	}
	if ( target->model != entity->hModel ) {
		target->meshes.DeleteContents( true );
		target->model = entity->hModel;
	}
	AddMesh( *target, mesh, r_maxEntityDecals.GetInteger() );
}

void ProjectOntoWorldModels( idRenderWorldLocal* world, const decalProjection_t& info, decalMesh_t* mesh, const idMaterial** onlyMaterials, int numOnlyMaterials ) {
	for ( int i = 0; i < world->BackendNumLocalModels(); i++ ) {
		const idRenderModel* model = world->BackendLocalModel( i );
		if ( model == NULL || !model->IsStaticWorldModel() ) {
			continue;
		}
		if ( !model->Bounds( NULL ).IntersectsBounds( info.bounds ) ) {
			continue;
		}
		CutModel( *mesh, model, info, onlyMaterials, numOnlyMaterials );
	}
}

// a decal's color now, or false once it has gone
bool MeshColor( const decalMesh_t& mesh, int time, idVec4& color ) {
	const int age = time - mesh.startTime;
	switch ( mesh.fade ) {
		case DF_NONE:
			color = mesh.startColor;
			return true;
		case DF_LIFETIME: {
			if ( mesh.lifeTime <= 0 || age >= mesh.lifeTime ) {
				return false;
			}
			const float f = age <= 0 ? 0.0f : static_cast< float >( age ) / mesh.lifeTime;
			color = mesh.startColor + ( mesh.endColor - mesh.startColor ) * f;
			return true;
		}
		case DF_MATERIAL:
		default: {
			const decalInfo_t info = mesh.material->GetDecalInfo();
			if ( age >= info.stayTime + info.fadeTime ) {
				return false;
			}
			if ( age <= info.stayTime || info.fadeTime <= 0 ) {
				color = mesh.startColor;
			} else {
				const float f = static_cast< float >( age - info.stayTime ) / info.fadeTime;
				color = mesh.startColor + ( mesh.endColor - mesh.startColor ) * f;
			}
			return true;
		}
	}
}

void RemoveFaded( decalTarget_t& target, int time ) {
	for ( int i = target.meshes.Num() - 1; i >= 0; i-- ) {
		idVec4 color;
		if ( !MeshColor( *target.meshes[ i ], time, color ) ) {
			delete target.meshes[ i ];
			target.meshes.RemoveIndex( i );
		}
	}
}

/*
One surface per material for a target in a view: the meshes' triangles with their
colors faded to this time, in per-view memory.
*/
void AddTargetSurfaces( decalTarget_t& target, const viewEntity_s* space, const renderEntity_t* entity, const idScreenRect& scissor, int time ) {
	RemoveFaded( target, time );
	if ( target.meshes.Num() == 0 || space == NULL ) {
		return;
	}
	const int surfID = space->maxSurfID > 0 ? space->maxSurfID - 1 : 0;	// past the model's own ids
	idList< bool > used;
	used.SetNum( target.meshes.Num() );
	memset( used.Begin(), 0, used.Num() * sizeof( bool ) );
	for ( int first = 0; first < target.meshes.Num(); first++ ) {
		if ( used[ first ] ) {
			continue;
		}
		const idMaterial* material = target.meshes[ first ]->material;
		int numVerts = 0;
		int numIndexes = 0;
		for ( int i = first; i < target.meshes.Num(); i++ ) {
			if ( !used[ i ] && target.meshes[ i ]->material == material ) {
				numVerts += target.meshes[ i ]->verts.Num();
				numIndexes += target.meshes[ i ]->indexes.Num();
			}
		}
		if ( numVerts == 0 || numIndexes == 0 || material == NULL || numVerts > 65535 ) {
			for ( int i = first; i < target.meshes.Num(); i++ ) {
				if ( target.meshes[ i ]->material == material ) {
					used[ i ] = true;
				}
			}
			continue;
		}
		frameGeometry_t geometry;
		geometry.tri = new srfTriangles_t();
		geometry.verts = new idDrawVert[ numVerts ];
		geometry.indexes = new glIndex_t[ numIndexes ];
		frameGeometry.Append( geometry );
		srfTriangles_t* tri = geometry.tri;
		tri->verts = geometry.verts;
		tri->indexes = geometry.indexes;
		tri->frameAlloced = true;
		tri->streamVertexCache = true;
		tri->bounds.Clear();
		for ( int i = first; i < target.meshes.Num(); i++ ) {
			decalMesh_t& mesh = *target.meshes[ i ];
			if ( used[ i ] || mesh.material != material ) {
				continue;
			}
			used[ i ] = true;
			idVec4 color;
			MeshColor( mesh, time, color );
			const int base = tri->numVerts;
			for ( int v = 0; v < mesh.verts.Num(); v++ ) {
				idDrawVert& out = tri->verts[ base + v ];
				out = mesh.verts[ v ];
				for ( int k = 0; k < 4; k++ ) {
					out.color[ k ] = static_cast< byte >( idMath::ClampInt( 0, 255, idMath::FtoiFast( color[ k ] * mesh.depthFade[ v ] * 255.0f ) ) );
				}
				tri->bounds.AddPoint( out.xyz );
			}
			for ( int n = 0; n < mesh.indexes.Num(); n++ ) {
				tri->indexes[ tri->numIndexes + n ] = static_cast< glIndex_t >( base + mesh.indexes[ n ] );
			}
			tri->numVerts += mesh.verts.Num();
			tri->numIndexes += mesh.indexes.Num();
		}
		tri->ambientCache = vertexCache.AllocFrameTemp( tri->verts, tri->numVerts * sizeof( tri->verts[ 0 ] ) );
		tri->indexCache = vertexCache.AllocFrameTemp( tri->indexes, tri->numIndexes * sizeof( tri->indexes[ 0 ] ) );
		if ( tri->ambientCache == NULL || tri->indexCache == NULL ) {
			continue;
		}
		tri->indexCache->indexBuffer = true;
		R_AddDrawSurf( tri, space, entity, material, scissor, surfID, NULL );
	}
}

} // namespace

/*
================
R_AddDecalSurfaces

From R_AddModelSurfaces: the world's decals in the view's world space, and the decals of
each visible entity space in it.
================
*/
void R_AddDecalSurfaces( viewEntity_s* const* spaces, int numSpaces ) {
	viewDef_s* view = RB_GetViewDef();
	if ( view == NULL || r_skipDecals.GetBool() ) {
		return;
	}
	const int time = view->renderView.time;
	if ( worldDecals.meshes.Num() > 0 ) {
		AddTargetSurfaces( worldDecals, &view->worldSpace, NULL, view->scissor, time );
	}
	if ( entityDecals.empty() ) {
		return;
	}
	for ( int i = 0; i < numSpaces; i++ ) {
		viewEntity_s* space = spaces[ i ];
		if ( space == NULL || space->culled || space->entityDef == NULL || space->entityIndex < 0 ) {
			continue;
		}
		auto found = entityDecals.find( space->entityIndex );
		if ( found == entityDecals.end() || found->second == NULL ) {
			continue;
		}
		if ( found->second->model != space->model ) {
			continue;	// the entity shows another model now (it is dropped at its next decal or its free)
		}
		AddTargetSurfaces( *found->second, space, space->entityDef, space->scissorRect, time );
	}
}

// from R_FreeBuiltDrawView
void R_FreeDecalViewGeometry( void ) {
	for ( int i = 0; i < frameGeometry.Num(); i++ ) {
		delete[] frameGeometry[ i ].verts;
		delete[] frameGeometry[ i ].indexes;
		delete frameGeometry[ i ].tri;
	}
	frameGeometry.SetNum( 0, false );
}

/*
================
idRenderWorldLocal decals
================
*/
idRenderModel* idRenderWorldLocal::CreateDecalModel() {
	idRenderModel* model = renderModelManager->AllocModel();
	model->InitEmpty( "_decalModel" );
	decalMesh_t* mesh = NewMesh( NULL, DF_NONE, 0 );
	loggedDecals[ model ] = mesh;
	return model;
}

/*
The deploy footprint (sdDeployMaskEditSession): the winding projected onto the surfaces
with one of onlyMaterials (the terrain's megatextures), in its color, added to a model
from CreateDecalModel; FinishDecal makes the model's surface from what was added. The
entity it is drawn with gives the material (customShader).
*/
void idRenderWorldLocal::AddToProjectedDecal( const idFixedWinding& winding, const idVec3& projectionOrigin, const bool parallel, const idVec4& color, idRenderModel* model, int entityNum, const idMaterial** onlyMaterials, const int numOnlyMaterials ) {
	( void )entityNum;
	auto found = loggedDecals.find( model );
	if ( found == loggedDecals.end() ) {
		return;
	}
	decalProjection_t info;
	if ( !CreateProjection( info, winding, projectionOrigin, parallel, 8.0f ) ) {
		return;
	}
	decalMesh_t piece;
	piece.material = NULL;
	piece.startTime = 0;
	piece.fade = DF_NONE;
	piece.lifeTime = 0;
	ProjectOntoWorldModels( this, info, &piece, onlyMaterials, numOnlyMaterials );
	decalMesh_t& mesh = *found->second;
	const int base = mesh.verts.Num();
	for ( int i = 0; i < piece.verts.Num(); i++ ) {
		idDrawVert v = piece.verts[ i ];
		for ( int k = 0; k < 4; k++ ) {
			v.color[ k ] = static_cast< byte >( idMath::ClampInt( 0, 255, idMath::FtoiFast( color[ k ] * 255.0f ) ) );
		}
		mesh.verts.Append( v );
	}
	for ( int i = 0; i < piece.indexes.Num(); i++ ) {
		mesh.indexes.Append( static_cast< glIndex_t >( base + piece.indexes[ i ] ) );
	}
}

void idRenderWorldLocal::ResetDecalModel( idRenderModel* model ) {
	auto found = loggedDecals.find( model );
	if ( found == loggedDecals.end() ) {
		return;
	}
	found->second->verts.SetNum( 0, false );
	found->second->indexes.SetNum( 0, false );
	FinishDecal( model );
}

void idRenderWorldLocal::FinishDecal( idRenderModel* model ) {
	auto found = loggedDecals.find( model );
	if ( found == loggedDecals.end() ) {
		return;
	}
	const decalMesh_t& mesh = *found->second;
	model->InitEmpty( "_decalModel" );
	if ( mesh.indexes.Num() == 0 ) {
		return;
	}
	// allocated as the static model allocates its own (Model.cpp AllocSurfaceTriangles),
	// since it frees them when the model is emptied again
	srfTriangles_t* tri = static_cast< srfTriangles_t* >( Mem_ClearedAlloc( sizeof( *tri ) ) );
	tri->numVerts = tri->numAllocedVerts = mesh.verts.Num();
	tri->numIndexes = tri->numAllocedIndices = mesh.indexes.Num();
	tri->mode = PM_TRIANGLE;
	tri->texCoordScale = 1.0f;
	tri->verts = static_cast< idDrawVert* >( Mem_ClearedAlloc( tri->numVerts * sizeof( *tri->verts ) ) );
	tri->indexes = static_cast< glIndex_t* >( Mem_Alloc( tri->numIndexes * sizeof( *tri->indexes ) ) );
	for ( int i = 0; i < tri->numVerts; i++ ) {
		tri->verts[ i ] = mesh.verts[ i ];
	}
	for ( int i = 0; i < tri->numIndexes; i++ ) {
		tri->indexes[ i ] = mesh.indexes[ i ];
	}
	tri->bounds.Clear();
	for ( int i = 0; i < tri->numVerts; i++ ) {
		tri->bounds.AddPoint( tri->verts[ i ].xyz );
	}
	modelSurface_t surface;
	surface.id = 0;
	surface.material = declManager->FindMaterial( "_default" );
	surface.geometry = tri;
	model->AddSurface( surface );
	model->FinishSurfaces( false, false );
}

/*
A decal onto the world: the static world models, and every static entity model the
projection touches (Doom 3 did both).
*/
void idRenderWorldLocal::ProjectDecalOntoWorld( const idFixedWinding& winding, const idVec3& projectionOrigin, const bool parallel, const float fadeDepth, const idMaterial* material, const int startTime, const int currentTime ) {
	( void )currentTime;
	if ( material == NULL ) {
		return;
	}
	decalProjection_t info;
	if ( !CreateProjection( info, winding, projectionOrigin, parallel, fadeDepth ) ) {
		return;
	}
	decalMesh_t* mesh = NewMesh( material, DF_MATERIAL, startTime );
	ProjectOntoWorldModels( this, info, mesh, NULL, 0 );
	AddMesh( worldDecals, mesh, r_maxWorldDecals.GetInteger() );
	for ( int handle = 0; handle < BackendNumEntityDefs(); handle++ ) {
		if ( BackendEntityDef( handle ) != NULL ) {
			ProjectOntoEntity( this, handle, info, material, startTime, DF_MATERIAL, NULL );
		}
	}
}

void idRenderWorldLocal::ProjectDecal( qhandle_t entityHandle, const idFixedWinding& winding, const idVec3& projectionOrigin, const bool parallel, const float fadeDepth, const idMaterial* material, const int startTime, const int currentTime ) {
	( void )currentTime;
	if ( material == NULL || entityHandle < 0 || entityHandle >= BackendNumEntityDefs() ) {
		return;
	}
	decalProjection_t info;
	if ( !CreateProjection( info, winding, projectionOrigin, parallel, fadeDepth ) ) {
		return;
	}
	ProjectOntoEntity( this, entityHandle, info, material, startTime, DF_MATERIAL, NULL );
}

void idRenderWorldLocal::ProjectOverlay( qhandle_t, const idPlane[2], const idMaterial* ) {
	// overlays (blood on animated models) are not drawn yet
}

void idRenderWorldLocal::RemoveDecals( qhandle_t entityHandle ) {
	auto found = entityDecals.find( entityHandle );
	if ( found != entityDecals.end() ) {
		delete found->second;
		entityDecals.erase( found );
	}
}

/*
A cheap decal (sdDeclDecal): a square of the declaration's size about the point, turned
at random, textured with one of its images, projected onto the entity (or the world for
-1) along the normal, fading from its start to its end color over its lifetime.
*/
void idRenderWorldLocal::AddCheapDecal( qhandle_t entityHandle, const cheapDecalParameters_t& params, float time ) {
	const sdDeclDecal* decl = params.decl;
	if ( decl == NULL || decl->GetMaterial() == NULL ) {
		return;
	}
	idVec3 normal = params.normal;
	if ( normal.Normalize() <= 0.0f ) {
		return;
	}
	const float size = decl->GetSpawnSize();
	if ( size <= 0.0f ) {
		return;
	}
	idVec3 right, up;
	normal.OrthogonalBasis( right, up );
	const float angle = idRandom::StaticRandom().RandomFloat() * idMath::TWO_PI;
	const float c = idMath::Cos( angle ) * size * 0.5f;
	const float s = idMath::Sin( angle ) * size * 0.5f;
	const idVec3 axisA = right * c + up * s;
	const idVec3 axisB = up * c - right * s;

	idVec2 stMin( 0.0f, 0.0f );
	idVec2 stMax( 1.0f, 1.0f );
	if ( decl->GetNumImages() > 0 ) {
		const sdBounds2D& image = decl->GetImage( idRandom::StaticRandom().RandomInt( decl->GetNumImages() ) );
		stMin = image.GetMins();
		stMax = image.GetMaxs();
	}
	const idVec3 center = params.origin + normal * ( size * 0.25f );
	idFixedWinding winding;
	winding.Clear();
	winding += idVec5( center - axisA - axisB, idVec2( stMin.x, stMin.y ) );
	winding += idVec5( center - axisA + axisB, idVec2( stMin.x, stMax.y ) );
	winding += idVec5( center + axisA + axisB, idVec2( stMax.x, stMax.y ) );
	winding += idVec5( center + axisA - axisB, idVec2( stMax.x, stMin.y ) );

	decalProjection_t info;
	if ( !CreateProjection( info, winding, center - normal * ( size * 0.5f ), true, size * 0.25f ) ) {
		return;
	}
	const int startTime = idMath::FtoiFast( time );
	idVec4 colors[ 3 ];
	colors[ 0 ] = decl->GetStartColor();
	colors[ 1 ] = decl->GetEndColor();
	colors[ 2 ].Set( decl->GetLifeTime() * 1000.0f, 0.0f, 0.0f, 0.0f );	// seconds in the decl
	if ( entityHandle < 0 ) {
		decalMesh_t* mesh = NewMesh( decl->GetMaterial(), DF_LIFETIME, startTime );
		mesh->startColor = colors[ 0 ];
		mesh->endColor = colors[ 1 ];
		mesh->lifeTime = static_cast< int >( colors[ 2 ].x );
		ProjectOntoWorldModels( this, info, mesh, NULL, 0 );
		AddMesh( worldDecals, mesh, r_maxWorldDecals.GetInteger() );
	} else if ( entityHandle < BackendNumEntityDefs() ) {
		ProjectOntoEntity( this, entityHandle, info, decl->GetMaterial(), startTime, DF_LIFETIME, colors );
	}
}

void idRenderWorldLocal::ClearDecals() {
	worldDecals.meshes.DeleteContents( true );
	worldDecals.model = NULL;
	for ( auto& entry : entityDecals ) {
		delete entry.second;
	}
	entityDecals.clear();
}
