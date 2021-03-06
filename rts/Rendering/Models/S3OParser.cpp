/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include <cctype>
#include <stdexcept>

#include "S3OParser.h"
#include "s3o.h"
#include "Game/GlobalUnsynced.h"
#include "Rendering/GlobalRendering.h"
#include "Rendering/Textures/S3OTextureHandler.h"
#include "Sim/Misc/CollisionVolume.h"
#include "System/Exceptions.h"
#include "System/StringUtil.h"
#include "System/Log/ILog.h"
#include "System/FileSystem/FileHandler.h"
#include "System/Platform/byteorder.h"



S3DModel CS3OParser::Load(const std::string& name)
{
	CFileHandler file(name);
	std::vector<unsigned char> fileBuf;

	if (!file.FileExists())
		throw content_error("[S3OParser] could not find model-file " + name);

	if (!file.IsBuffered()) {
		fileBuf.resize(file.FileSize(), 0);
		file.Read(fileBuf.data(), fileBuf.size());
	} else {
		fileBuf = std::move(file.GetBuffer());
	}

	S3OHeader header;
	memcpy(&header, fileBuf.data(), sizeof(header));
	header.swap();

	S3DModel model;
		model.name = name;
		model.type = MODELTYPE_S3O;
		model.numPieces = 0;
		model.texs[0] = (header.texture1 == 0)? "" : (char*) &fileBuf[header.texture1];
		model.texs[1] = (header.texture2 == 0)? "" : (char*) &fileBuf[header.texture2];
		model.mins = DEF_MIN_SIZE;
		model.maxs = DEF_MAX_SIZE;

	texturehandlerS3O->PreloadTexture(&model);

	model.FlattenPieceTree(LoadPiece(&model, nullptr, fileBuf.data(), header.rootPiece));

	// set after the extrema are known
	model.radius = (header.radius <= 0.01f)? model.CalcDrawRadius(): header.radius;
	model.height = (header.height <= 0.01f)? model.CalcDrawHeight(): header.height;
	model.relMidPos = float3(header.midx, header.midy, header.midz);

	return model;
}

SS3OPiece* CS3OParser::LoadPiece(S3DModel* model, SS3OPiece* parent, unsigned char* buf, int offset)
{
	model->numPieces++;

	// retrieve piece data
	Piece* fp = (Piece*)&buf[offset]; fp->swap();
	Vertex* vertexList = reinterpret_cast<Vertex*>(&buf[fp->vertices]);

	const int* indexList = reinterpret_cast<int*>(&buf[fp->vertexTable]);
	const int* childList = reinterpret_cast<int*>(&buf[fp->children]);

	// create piece
	SS3OPiece* piece = new SS3OPiece();
		piece->offset.x = fp->xoffset;
		piece->offset.y = fp->yoffset;
		piece->offset.z = fp->zoffset;
		piece->primType = fp->primitiveType;
		piece->name = (char*) &buf[fp->name];
		piece->parent = parent;

	// retrieve vertices
	piece->SetVertexCount(fp->numVertices);
	for (int a = 0; a < fp->numVertices; ++a) {
		Vertex* v = (vertexList++);
		v->swap();

		SS3OVertex sv;
		sv.pos = float3(v->xpos, v->ypos, v->zpos);
		sv.normal = float3(v->xnormal, v->ynormal, v->znormal).SafeANormalize();
		sv.texCoords[0] = float2(v->texu, v->texv);
		sv.texCoords[1] = float2(v->texu, v->texv);
		sv.pieceIndex = model->numPieces - 1;

		piece->SetVertex(a, sv);
	}

	// retrieve draw indices
	piece->SetIndexCount(fp->vertexTableSize);
	for (int a = 0; a < fp->vertexTableSize; ++a) {
		const int vertexDrawIdx = swabDWord(*(indexList++));
		piece->SetIndex(a, vertexDrawIdx);
	}

	// post-process the piece
	{
		piece->SetGlobalOffset(CMatrix44f::Identity());
		piece->Trianglize();
		piece->SetVertexTangents();
		piece->SetMinMaxExtends();

		model->mins = float3::min(piece->goffset + piece->mins, model->mins);
		model->maxs = float3::max(piece->goffset + piece->maxs, model->maxs);

		piece->SetCollisionVolume(CollisionVolume('b', 'z', piece->maxs - piece->mins, (piece->maxs + piece->mins) * 0.5f));
	}

	// load children pieces
	piece->children.reserve(fp->numchildren);

	for (int a = 0; a < fp->numchildren; ++a) {
		const int childOffset = swabDWord(*(childList++));

		piece->children.push_back(LoadPiece(model, piece, buf, childOffset));
	}

	return piece;
}






void SS3OPiece::SetMinMaxExtends()
{
	for (const SS3OVertex& v: vertices) {
		mins = float3::min(mins, v.pos);
		maxs = float3::max(maxs, v.pos);
	}
}


void SS3OPiece::Trianglize()
{
	switch (primType) {
		case S3O_PRIMTYPE_TRIANGLES: {
		} break;
		case S3O_PRIMTYPE_TRIANGLE_STRIP: {
			if (indices.size() < 3) {
				primType = S3O_PRIMTYPE_TRIANGLES;
				indices.clear();
				return;
			}

			decltype(indices) newIndices;
			newIndices.resize(indices.size() * 3); // each index (can) create a new triangle

			for (size_t i = 0; (i + 2) < indices.size(); ++i) {
				// indices can contain end-of-strip markers (-1U)
				if (indices[i + 0] == -1 || indices[i + 1] == -1 || indices[i + 2] == -1)
					continue;

				newIndices.push_back(indices[i + 0]);
				newIndices.push_back(indices[i + 1]);
				newIndices.push_back(indices[i + 2]);
			}

			primType = S3O_PRIMTYPE_TRIANGLES;
			indices.swap(newIndices);
		} break;
		case S3O_PRIMTYPE_QUADS: {
			if (indices.size() % 4 != 0) {
				primType = S3O_PRIMTYPE_TRIANGLES;
				indices.clear();
				return;
			}

			decltype(indices) newIndices;
			const size_t oldCount = indices.size();
			newIndices.resize(oldCount + oldCount / 2); // 4 indices become 6

			for (size_t i = 0, j = 0; i < indices.size(); i += 4) {
				newIndices[j++] = indices[i + 0];
				newIndices[j++] = indices[i + 1];
				newIndices[j++] = indices[i + 2];

				newIndices[j++] = indices[i + 0];
				newIndices[j++] = indices[i + 2];
				newIndices[j++] = indices[i + 3];
			}

			primType = S3O_PRIMTYPE_TRIANGLES;
			indices.swap(newIndices);
		} break;

		default: {
		} break;
	}
}


void SS3OPiece::SetVertexTangents()
{
	if (!HasGeometryData())
		return;

	if (primType == S3O_PRIMTYPE_QUADS)
		return;

	unsigned stride = 0;

	switch (primType) {
		case S3O_PRIMTYPE_TRIANGLES: {
			stride = 3;
		} break;
		case S3O_PRIMTYPE_TRIANGLE_STRIP: {
			stride = 1;
		} break;
	}

	// for triangle strips, the piece vertex _indices_ are defined
	// by the draw order of the vertices numbered <v, v + 1, v + 2>
	// for v in [0, n - 2]
	const unsigned vrtMaxNr = (stride == 1)?
		indices.size() - 2:
		indices.size();

	// set the triangle-level S- and T-tangents
	for (unsigned vrtNr = 0; vrtNr < vrtMaxNr; vrtNr += stride) {
		bool flipWinding = false;

		if (primType == S3O_PRIMTYPE_TRIANGLE_STRIP) {
			flipWinding = ((vrtNr & 1) == 1);
		}

		const int v0idx = indices[vrtNr                      ];
		const int v1idx = indices[vrtNr + (flipWinding? 2: 1)];
		const int v2idx = indices[vrtNr + (flipWinding? 1: 2)];

		if (v1idx == -1 || v2idx == -1) {
			// not a valid triangle, skip
			// to start of next tri-strip
			vrtNr += 3; continue;
		}

		const SS3OVertex* vrt0 = &vertices[v0idx];
		const SS3OVertex* vrt1 = &vertices[v1idx];
		const SS3OVertex* vrt2 = &vertices[v2idx];

		const float3& p0 = vrt0->pos;
		const float3& p1 = vrt1->pos;
		const float3& p2 = vrt2->pos;

		const float2& tc0 = vrt0->texCoords[0];
		const float2& tc1 = vrt1->texCoords[0];
		const float2& tc2 = vrt2->texCoords[0];

		const float x1x0 = p1.x - p0.x, x2x0 = p2.x - p0.x;
		const float y1y0 = p1.y - p0.y, y2y0 = p2.y - p0.y;
		const float z1z0 = p1.z - p0.z, z2z0 = p2.z - p0.z;

		const float s1 = tc1.x - tc0.x, s2 = tc2.x - tc0.x;
		const float t1 = tc1.y - tc0.y, t2 = tc2.y - tc0.y;

		// if d is 0, texcoors are degenerate
		const float d = (s1 * t2 - s2 * t1);
		const bool  b = (d > -0.0001f && d < 0.0001f);
		const float r = b? 1.0f: 1.0f / d;

		// note: not necessarily orthogonal to each other
		// or to vertex normal (only to the triangle plane)
		const float3 sdir((t2 * x1x0 - t1 * x2x0) * r, (t2 * y1y0 - t1 * y2y0) * r, (t2 * z1z0 - t1 * z2z0) * r);
		const float3 tdir((s1 * x2x0 - s2 * x1x0) * r, (s1 * y2y0 - s2 * y1y0) * r, (s1 * z2z0 - s2 * z1z0) * r);

		vertices[v0idx].sTangent += sdir;
		vertices[v1idx].sTangent += sdir;
		vertices[v2idx].sTangent += sdir;

		vertices[v0idx].tTangent += tdir;
		vertices[v1idx].tTangent += tdir;
		vertices[v2idx].tTangent += tdir;
	}

	// set the smoothed per-vertex tangents
	for (int vrtIdx = vertices.size() - 1; vrtIdx >= 0; vrtIdx--) {
		float3& n = vertices[vrtIdx].normal;
		float3& s = vertices[vrtIdx].sTangent;
		float3& t = vertices[vrtIdx].tTangent;
		int h = 1;

		if (math::isnan(n.x) || math::isnan(n.y) || math::isnan(n.z)) {
			n = FwdVector;
		}
		if (s == ZeroVector) { s = RgtVector; }
		if (t == ZeroVector) { t =  UpVector; }

		h = ((n.cross(s)).dot(t) < 0.0f)? -1: 1;
		s = (s - n * n.dot(s));
		s = s.SafeANormalize();
		t = (s.cross(n)) * h;

		// t = (s.cross(n));
		// h = ((s.cross(t)).dot(n) >= 0.0f)? 1: -1;
		// t = t * h;
	}
}
