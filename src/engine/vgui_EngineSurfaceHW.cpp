#include "quakedef.h"
#include "cl_main.h"
#include "qgl.h"
#include "gl_rmain.h"
#include "render.h"
#include "sys_getmodes.h"
#include "vgui_EngineSurface.h"
#include "vgui_int.h"

#include <UtlRBTree.h>

bool g_bScissor = false;
Rect_t g_ScissorRect = {};

struct TCoordRect
{
	float s0;
	float t0;
	float s1;
	float t1;
};

bool ScissorRect_TCoords( int x0, int y0, int x1, int y1,
						  float s0, float t0, float s1, float t1,
						  Rect_t* pOut, TCoordRect* pTCoords )
{
	bool bResult = false;

	if( g_bScissor )
	{
		if( pOut )
		{
			if( g_ScissorRect.x == g_ScissorRect.width &&
				g_ScissorRect.y == g_ScissorRect.height ||
				( x0 == x1 && y0 == y1 ) ||
				x1 <= g_ScissorRect.x ||
				x0 >= g_ScissorRect.width ||
				y1 <= g_ScissorRect.y ||
				y0 >= g_ScissorRect.height )
			{
				pOut->x = 0;
				pOut->y = 0;
				pOut->width = 0;
				pOut->height = 0;
			}
			else
			{
				pOut->x = g_ScissorRect.x;

				if( x0 >= pOut->x )
					pOut->x = x0;

				pOut->width = g_ScissorRect.width;

				if( x1 <= pOut->width )
					pOut->width = x1;

				pOut->y = g_ScissorRect.y;

				if( y0 >= pOut->y )
					pOut->y = y0;

				pOut->height = g_ScissorRect.height;

				if( y1 <= pOut->height )
					pOut->height = y1;

				if( pTCoords )
				{
					pTCoords->s0 = static_cast<double>( pOut->x - x0 ) / ( x1 - x0 ) * ( s1 - s0 ) + s0;
					pTCoords->s1 = static_cast<double>( pOut->width - x0 ) / ( x1 - x0 ) * ( s1 - s0 ) + s0;
				
					pTCoords->t0 = static_cast<double>( pOut->y - y0 ) / ( y1 - y0 ) * ( t1 - t0 ) + t0;
					pTCoords->t1 = static_cast<double>( pOut->height - y0 ) / ( y1 - y0 ) * ( t1 - t0 ) + t0;
				}

				bResult = true;
			}
		}
	}
	else
	{
		bResult = true;

		pOut->x = x0;
		pOut->width = x1;
		pOut->y = y0;
		pOut->height = y1;

		if( pTCoords )
		{
			pTCoords->s0 = s0;
			pTCoords->t0 = t0;
			pTCoords->s1 = s1;
			pTCoords->t1 = t1;
		}
	}

	return bResult;
}

namespace
{
struct Texture
{
	int _id;
	int _wide;
	int _tall;
	float _s0;
	float _t0;
	float _s1;
	float _t1;
};
}

struct VertexBuffer_t
{
	float texcoords[ 2 ];
	float vertex[ 2 ];
};

bool VguiSurfaceTexturesLessFunc( const Texture& lhs, const Texture& rhs )
{
	return lhs._id < rhs._id;
}

static CUtlRBTree<Texture, int> g_VGuiSurfaceTextures( &VguiSurfaceTexturesLessFunc );
static Texture* staticTextureCurrent = nullptr;

static VertexBuffer_t g_VertexBuffer[ 256 ];
static int g_iVertexBufferEntriesUsed = 0;
static float g_vgui_projection_matrix[ 16 ];

static Texture* staticGetTextureById( int id )
{
	Texture dummy;
	dummy._id = id;

	auto index = g_VGuiSurfaceTextures.Find( dummy );

	if( index != g_VGuiSurfaceTextures.InvalidIndex() )
		return &g_VGuiSurfaceTextures[ index ];

	return nullptr;
}

void EngineSurface::createRenderPlat()
{
	//Nothing
}

void EngineSurface::deleteRenderPlat()
{
	//Nothing
}

void EngineSurface::pushMakeCurrent( int* insets, int* absExtents, int* clipRect, bool translateToScreenSpace )
{
	int x = 0, y = 0;

	if( translateToScreenSpace )
	{
		if( VideoMode_IsWindowed() )
		{
			SDL_GetWindowPosition( pmainwindow, &x, &y );
		}
	}

	Rect_t rect;

	rect.y = 0;

	if( VideoMode_IsWindowed() )
	{
		SDL_GetWindowSize( pmainwindow, &rect.width, &rect.height );
	}
	else
	{
		VideoMode_GetCurrentVideoMode( &rect.width, &rect.height, nullptr );
	}

	rect.height += rect.y;

	//TODO: operations out of order? - Solokiller
	qglPushMatrix();
	qglMatrixMode( GL_PROJECTION );
	qglLoadIdentity();
	qglOrtho( 0, rect.width, rect.height, 0, -1, 1 );

	qglMatrixMode( GL_MODELVIEW );
	qglPushMatrix();
	qglLoadIdentity();

	int wide = insets[ 0 ];
	int tall = insets[ 1 ];

	qglTranslatef( wide, tall, 0 );

	const int iOffsetX = absExtents[ 0 ] - x;
	const int iOffsetY = absExtents[ 1 ] - y;
	qglTranslatef( iOffsetX, iOffsetY, 0 );

	g_bScissor = true;

	g_ScissorRect.x = clipRect[ 0 ] - x - ( insets[ 0 ] + iOffsetX );
	g_ScissorRect.width = clipRect[ 2 ] - x - ( insets[ 0 ] + iOffsetX );
	g_ScissorRect.y = clipRect[ 1 ] - y - ( insets[ 1 ] + iOffsetY );
	g_ScissorRect.height = clipRect[ 3 ] - y - ( insets[ 1 ] + iOffsetY );

	qglEnable( GL_BLEND );
	qglBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
}

void EngineSurface::popMakeCurrent()
{
	drawFlushText();

	//TODO: operations out of order? - Solokiller
	qglMatrixMode( GL_PROJECTION );
	qglMatrixMode( GL_MODELVIEW );
	qglPopMatrix();
	qglPopMatrix();

	g_bScissor = false;

	qglEnable( GL_TEXTURE_2D );
}

void EngineSurface::drawFilledRect( int x0, int y0, int x1, int y1 )
{
	if( _drawColor[ 3 ] != 255 )
	{
		Rect_t rcOut;

		if( ScissorRect_TCoords( x0, y0, x1, y1, 0, 0, 0, 0, &rcOut, nullptr ) )
		{
			qglDisable( GL_TEXTURE_2D );

			qglColor4ub( _drawColor[ 0 ], _drawColor[ 1 ], _drawColor[ 2 ], 255 - _drawColor[ 3 ] );
		
			qglBegin( GL_QUADS );

			qglVertex2f( rcOut.x, rcOut.y );
			qglVertex2f( rcOut.width, rcOut.y );
			qglVertex2f( rcOut.width, rcOut.height );
			qglVertex2f( rcOut.x, rcOut.height );

			qglEnd();

			qglEnable( GL_TEXTURE_2D );
		}
	}
}

void EngineSurface::drawOutlinedRect( int x0, int y0, int x1, int y1 )
{
	if( _drawColor[ 3 ] != 255 )
	{
		qglDisable( GL_TEXTURE_2D );
		qglColor4ub( _drawColor[ 0 ], _drawColor[ 1 ], _drawColor[ 2 ], 255 - _drawColor[ 3 ] );

		drawFilledRect( x0, y0, x1, y0 + 1 );
		drawFilledRect( x0, y1 - 1, x1, y1 );
		drawFilledRect( x0, y0 + 1, x0 + 1, y1 - 1 );
		drawFilledRect( x1 - 1, y0 + 1, x1, y1 - 1 );
	}
}

void EngineSurface::drawLine( int x1, int y1, int x2, int y2 )
{
	qglDisable( GL_TEXTURE_2D );
	qglEnable( GL_BLEND );
	qglTexEnvf( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );
	qglBlendFunc( GL_SRC_ALPHA, GL_ONE );

	qglColor4ub( _drawColor[ 0 ], _drawColor[ 1 ], _drawColor[ 2 ], 255 - _drawColor[ 3 ] );

	qglBegin( GL_LINES );

	qglVertex2f( x1, y1 );
	qglVertex2f( x2, y2 );

	qglEnd();

	qglColor3f( 1, 1, 1 );

	qglEnable( GL_TEXTURE_2D );
	qglDisable( GL_BLEND );
}

void EngineSurface::drawPolyLine( int* px, int* py, int numPoints )
{
	qglDisable( GL_TEXTURE_2D );
	qglEnable( GL_BLEND );
	qglTexEnvf( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );
	qglBlendFunc( GL_SRC_ALPHA, GL_ONE );

	qglColor4ub( _drawColor[ 0 ], _drawColor[ 1 ], _drawColor[ 2 ], 255 - _drawColor[ 3 ] );

	qglBegin( GL_LINES );

	for( int i = 1; i < numPoints; ++i )
	{
		qglVertex2f( px[ i - 1 ], py[ i - 1 ] );
		qglVertex2f( px[ i ], py[ i ] );
	}

	qglEnd();

	qglColor3f( 1, 1, 1 );

	qglEnable( GL_TEXTURE_2D );
	qglDisable( GL_BLEND );
}

void EngineSurface::drawTexturedPolygon( vgui2::VGuiVertex* pVertices, int n )
{
	if( _drawColor[ 3 ] == 255 )
		return;

	qglColor4ub(
		_drawColor[ 0 ],
		_drawColor[ 1 ],
		_drawColor[ 2 ],
		255 - _drawColor[ 3 ]
	);

	qglEnable( GL_TEXTURE_2D );

	auto pNext = pVertices;

	qglBegin( GL_POLYGON );

	for( int i = 0; i < n; ++i, ++pNext )
	{
		qglTexCoord2f( pNext->u, pNext->v );
		qglVertex2f( pNext->x, pNext->y );
	}

	qglEnd();
}

void EngineSurface::drawSetTextureRGBA( int id, const byte* rgba, int wide, int tall, int hardwareFilter, int hasAlphaChannel )
{
	auto pTexture = staticGetTextureById( id );

	if( !pTexture )
	{
		Texture newTexture;

		memset( &newTexture, 0, sizeof( newTexture ) );

		newTexture._id = id;
		
		pTexture = &g_VGuiSurfaceTextures[ g_VGuiSurfaceTextures.Insert( newTexture ) ];
	}

	if( !pTexture )
		return;

	pTexture->_id = id;
	pTexture->_wide = wide;
	pTexture->_tall = tall;

	int pow2Wide;
	int pow2Tall;

	for( int i = 0; i < 32; ++i )
	{
		pow2Wide = 1 << i;

		if( wide <= pow2Wide )
			break;
	}

	for( int i = 0; i < 32; ++i )
	{
		pow2Tall = 1 << i;

		if( tall <= pow2Tall )
			break;
	}

	int* pExpanded = nullptr;

	const void* pData = rgba;

	//Convert to power of 2 texture.
	if( wide != pow2Wide || tall != pow2Tall )
	{
		pExpanded = new int[ pow2Wide * pow2Tall ];

		pData = pExpanded;

		memset( pExpanded, 0, 4 * pow2Wide * pow2Tall );

		const int* pSrc = reinterpret_cast<const int*>( rgba );
		int* pDest = pExpanded;

		for( int y = 0; y < tall; ++y )
		{
			for( int x = 0; x < wide; ++x )
			{
				pDest[ x ] = pSrc[ x ];
			}

			pDest += pow2Wide;
			pSrc += wide;
		}
	}

	pTexture->_s0 = 0;
	pTexture->_t0 = 0;
	pTexture->_s1 = static_cast<double>( wide ) / pow2Wide;
	pTexture->_t1 = static_cast<double>( tall ) / pow2Tall;

	staticTextureCurrent = pTexture;

	currenttexture = id;

	qglBindTexture( GL_TEXTURE_2D, id );

	if( hardwareFilter )
	{
		qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
		qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	}
	else
	{
		qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
		qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
	}

	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	qglTexEnvf( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );

	int w, h, bpp;
	VideoMode_GetCurrentVideoMode( &w, &h, &bpp );

	if( hasAlphaChannel || bpp == 32 )
	{
		qglTexImage2D(
			GL_TEXTURE_2D,
			0, GL_RGBA8,
			pow2Wide, pow2Tall,
			0,
			GL_RGBA, GL_UNSIGNED_BYTE,
			pData
		);
	}
	else
	{
		qglTexImage2D(
			GL_TEXTURE_2D,
			0, GL_RGB5_A1,
			pow2Wide, pow2Tall,
			0,
			GL_RGBA, GL_UNSIGNED_BYTE,
			pData
		);

		if( pExpanded )
			delete[] pExpanded;
	}
}

void EngineSurface::drawSetTexture( int id )
{
	if( id != currenttexture )
	{
		drawFlushText();
		currenttexture = id;
	}

	staticTextureCurrent = staticGetTextureById( id );

	qglBindTexture( GL_TEXTURE_2D, id );
}

void EngineSurface::drawTexturedRect( int x0, int y0, int x1, int y1 )
{
	if( !staticTextureCurrent || _drawColor[ 3 ] == 255 )
		return;

	Rect_t rcOut;
	TCoordRect tRect;

	if( !ScissorRect_TCoords(
		x0, y0, x1, y1,
		staticTextureCurrent->_s0,
		staticTextureCurrent->_t0,
		staticTextureCurrent->_s1,
		staticTextureCurrent->_t1,
		&rcOut,
		&tRect )
	)
	{
		return;
	}

	qglEnable( GL_TEXTURE_2D );

	qglBindTexture( GL_TEXTURE_2D, staticTextureCurrent->_id );

	currenttexture = staticTextureCurrent->_id;

	qglColor4ub(
		_drawColor[ 0 ],
		_drawColor[ 1 ],
		_drawColor[ 2 ],
		255 - _drawColor[ 3 ]
	);

	qglBegin( GL_QUADS );

	qglTexCoord2f( tRect.s0, tRect.t0 );
	qglVertex2f( rcOut.x, rcOut.y );

	qglTexCoord2f( tRect.s1, tRect.t0 );
	qglVertex2f( rcOut.width, rcOut.y );

	qglTexCoord2f( tRect.s1, tRect.t1 );
	qglVertex2f( rcOut.width, rcOut.height );

	qglTexCoord2f( tRect.s0, tRect.t1 );
	qglVertex2f( rcOut.x, rcOut.height );

	qglEnd();
}

void EngineSurface::drawPrintChar( int x, int y, int wide, int tall, float s0, float t0, float s1, float t1 )
{
	//TODO: implement - Solokiller
}

void EngineSurface::drawPrintCharAdd( int x, int y, int wide, int tall, float s0, float t0, float s1, float t1 )
{
	//TODO: implement - Solokiller
}

void EngineSurface::drawSetTextureFile( int id, const char* filename, int hardwareFilter, bool forceReload )
{
	auto pTexture = staticGetTextureById( id );

	if( !pTexture || forceReload )
	{
		char name[ 512 ];

		byte buf[ 512 * 512 ];

		int width, height;

		snprintf( name, ARRAYSIZE( name ), "%s.tga", filename );

		bool bLoaded = false;

		if( LoadTGA( name, buf, sizeof( buf ), &width, &height ) )
		{
			bLoaded = true;
		}
		else
		{
			snprintf( name, ARRAYSIZE( name ), "%s.bmp", filename );

			FileHandle_t hFile = FS_Open( name, "rb" );

			if( hFile != FILESYSTEM_INVALID_HANDLE )
			{
				VGui_LoadBMP( hFile, buf, sizeof( buf ), &width, &height );
				//TODO: check result of call - Solokiller
				bLoaded = true;
			}
		}

		if( bLoaded )
		{
			drawSetTextureRGBA( id, buf, width, height, hardwareFilter, false );

			pTexture = staticGetTextureById( id );
		}

		if( fs_perf_warnings.value && ( !IsPowerOfTwo( width ) || !IsPowerOfTwo( height ) ) )
		{
			Con_DPrintf( "fs_perf_warnings: Resampling non-power-of-2 image '%s' (%dx%d)\n", filename, width, height );
		}
	}

	if( pTexture )
		drawSetTexture( id );
}

void EngineSurface::drawGetTextureSize( int id, int* wide, int* tall )
{
	auto pTexture = staticGetTextureById( id );

	if( pTexture )
	{
		*wide = pTexture->_wide;
		*tall = pTexture->_tall;
	}
	else
	{
		*wide = 0;
		*tall = 0;
	}
}

int EngineSurface::isTextureIdValid( int id )
{
	return staticGetTextureById( id ) != nullptr;
}

void EngineSurface::drawSetSubTextureRGBA( int textureID, int drawX, int drawY, const byte* rgba, int subTextureWide, int subTextureTall )
{
	qglTexSubImage2D(
		GL_TEXTURE_2D,
		0,
		drawX, drawY,
		subTextureWide, subTextureTall,
		GL_RGBA, GL_UNSIGNED_BYTE,
		rgba
	);
}

void EngineSurface::drawFlushText()
{
	if( g_iVertexBufferEntriesUsed )
	{
		qglEnableClientState( GL_VERTEX_ARRAY );
		qglEnableClientState( GL_TEXTURE_COORD_ARRAY );

		qglEnable( GL_TEXTURE_2D );
		qglEnable( GL_BLEND );

		qglTexEnvf( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );

		qglBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );

		qglColor4ub(
			_drawTextColor[ 0 ],
			_drawTextColor[ 1 ],
			_drawTextColor[ 2 ],
			255 - _drawTextColor[ 3 ]
			);

		qglTexCoordPointer( 2, GL_FLOAT, sizeof( float ) * 4, g_VertexBuffer[ 0 ].texcoords );
		qglVertexPointer( 2, GL_FLOAT, sizeof( float ) * 4, g_VertexBuffer[ 0 ].vertex );

		qglDrawArrays( GL_QUADS, 0, g_iVertexBufferEntriesUsed );

		qglDisable( GL_BLEND );

		g_iVertexBufferEntriesUsed = 0;
	}
}

void EngineSurface::resetViewPort()
{
	//Nothing
}

void EngineSurface::drawSetTextureBGRA( int id, const byte* rgba, int wide, int tall, int hardwareFilter, int hasAlphaChannel )
{
	//TODO: implement - Solokiller
}

void EngineSurface::drawUpdateRegionTextureBGRA( int nTextureID, int x, int y, const byte* pchData, int wide, int tall )
{
	auto pTexture = staticGetTextureById( nTextureID );

	qglBindTexture( GL_TEXTURE_2D, nTextureID );

	qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0 );
	qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );

	qglPixelStorei( GL_UNPACK_ROW_LENGTH, pTexture->_wide );

	qglTexSubImage2D(
		GL_TEXTURE_2D,
		0,
		0, y,
		pTexture->_wide, tall,
		GL_BGRA, GL_UNSIGNED_BYTE,
		&pchData[ 4 * y * pTexture->_wide ]
	);

	glPixelStorei( GL_UNPACK_ROW_LENGTH, 0 );
}
