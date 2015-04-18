#include <node.h>
#include <v8.h>
#include <memory>
#include "node.h"
#include "node_buffer.h"

#include <algorithm>
#include <Alembic/Abc/All.h>
#include <Alembic/AbcGeom/All.h>
#include <Alembic/AbcCoreHDF5/All.h>
namespace AbcA = Alembic::AbcCoreAbstract;

using namespace v8;

template <class T> std::string to_string(T value)
{
	std::stringstream converter;
	std::string  str;
	converter << value;
	converter >> str;
	return str;
}

class AlembicArchive {
public:

	static AlembicArchive& instance() {
		static AlembicArchive instance;
		return instance;
	}

	Alembic::Abc::OArchive * archive;
	AbcA::uint32_t timeindex;
	AbcA::TimeSamplingPtr timesampling;

	typedef std::map<int, Alembic::AbcGeom::OXform > XformMap;
	XformMap xform_map;

	typedef std::map<int, Alembic::AbcGeom::OXformSchema > XformSchemaMap;
	XformSchemaMap xform_schema_map;

	typedef std::map<int, Alembic::AbcGeom::OPolyMesh > MeshMap;
	MeshMap mesh_map;

	typedef std::map<int, Alembic::AbcGeom::OPolyMeshSchema > SchemaMap;
	SchemaMap schema_map;
	
	typedef std::map<int, Alembic::AbcGeom::OCamera > CameraMap;
	CameraMap camera_map;

	typedef std::map<int, Alembic::AbcGeom::OCameraSchema  > CameraSchemaMap;
	CameraSchemaMap camera_schema_map;

	typedef std::map<int, int> FaceToVertexIndex;
	typedef std::map<int, FaceToVertexIndex> FaceToVertexIndexMap;
	FaceToVertexIndexMap face_to_vertex_index_map;

	bool is_export_normals;
	bool is_export_uvs;
	int export_mode;
	bool is_use_euler_rotation_camera;

	void end() {
		timeindex = 0;
		timesampling = AbcA::TimeSamplingPtr();
		xform_map.clear();
		xform_schema_map.clear();
		mesh_map.clear();
		schema_map.clear();
		camera_map.clear();
		camera_schema_map.clear();
		face_to_vertex_index_map.clear();
		{
			delete archive; archive = NULL;
		}
	}
private:
	AlembicArchive() : archive(NULL), timeindex(0), export_mode(0), is_use_euler_rotation_camera(false) {}
};

typedef std::pair<int, int> IndexPair;

class MeshBuffer
{
public:
	std::vector<int> vertex_index_buffer;
	std::vector<int> normal_index_buffer;
	std::vector<int> uv_index_buffer;
	std::vector<int> vertex_per_poly_buffer;
	std::vector<int> poly_mat_buffer;
	std::vector<Imath::V3f> vertex_buffer;
	std::vector<Imath::V3f> normal_buffer;
	std::vector<Imath::V3f> uv_buffer;
};

void export_alembic_xform_by_material_fix_vindex(
	const MeshBuffer & mesh_buffer, 
	int mesh_buffer_index)
{
	AlembicArchive &archive = AlembicArchive::instance();
	Alembic::AbcGeom::OObject top_obj(*archive.archive, Alembic::AbcGeom::kTop);

	// sort material index with index
	const int polygon_count = static_cast<int>(mesh_buffer.poly_mat_buffer.size());
	if (polygon_count <= 0) { return; }
	std::vector< IndexPair > index_pair_list(polygon_count);
	std::vector<int> start_vertex_number_list(polygon_count);
	std::map<int, int> material_counter;
	int start_vertex_number = 0;
	for (int i = 0; i < polygon_count; ++i)
	{
		const int material = mesh_buffer.poly_mat_buffer[i];
		const int vert_per_poly = mesh_buffer.vertex_per_poly_buffer[i];
		index_pair_list[i] = IndexPair(material, static_cast<int>(i));
		start_vertex_number_list[i] = start_vertex_number;
		start_vertex_number += vert_per_poly;
		int vertex_per_material = material_counter[material];
		material_counter[material] = vertex_per_material + vert_per_poly;
	}
	std::sort(index_pair_list.begin(), index_pair_list.end());

	// store meesh by material order
	int current_poly = 0;
	int current_mat = index_pair_list[0].first;
	for (int i = 0, mat_count = static_cast<int>(material_counter.size()); i < mat_count; ++i)
	{
		const int key = mesh_buffer_index * 10000 + i;
		const std::string mesh_name = "mesh_" + to_string(mesh_buffer_index) + "_material_" + to_string(i);
		const std::string xform_name = "xform_" + to_string(mesh_buffer_index) + "_material_" + to_string(i);

		// create or get xform
		Alembic::AbcGeom::OXform xform;
		if (archive.xform_map.find(key) != archive.xform_map.end()) {
			xform = archive.xform_map[key];
		} else {
			xform = Alembic::AbcGeom::OXform(top_obj, xform_name, archive.timesampling);
			archive.xform_map[key] = xform;
		}

		// create or get mesh
		bool is_first_mesh = false;
		Alembic::AbcGeom::OPolyMesh poly_mesh;
		if (archive.mesh_map.find(key) != archive.mesh_map.end()) {
			poly_mesh = archive.mesh_map[key];
		} else {
			poly_mesh = Alembic::AbcGeom::OPolyMesh(xform, mesh_name, archive.timesampling);
			archive.mesh_map[key] = poly_mesh;
			Alembic::AbcGeom::OPolyMeshSchema &meshSchema = poly_mesh.getSchema();
			archive.schema_map[key] = meshSchema;
			is_first_mesh = true;
		}

		// create face to vertex index
		if (archive.face_to_vertex_index_map.find(key) == archive.face_to_vertex_index_map.end())
		{
			AlembicArchive::FaceToVertexIndex fi_to_vi;
			archive.face_to_vertex_index_map[key] = fi_to_vi;
		}

		// get mesh schema
		Alembic::AbcGeom::OPolyMeshSchema &meshSchema = archive.schema_map[key];
		meshSchema.setTimeSampling(archive.timesampling);

		// iterate same material polygons 
		std::vector<Imath::V3f> vertex_list_by_material;
		std::vector<Imath::V3f> normal_list_by_material;
		std::vector<Imath::V2f> uv_list_by_material;
		std::vector<Alembic::Util::int32_t> face_list_by_material;
		std::vector<Alembic::Util::int32_t> facecount_list_by_material;
		AlembicArchive::FaceToVertexIndex& fi_to_vi = archive.face_to_vertex_index_map[key];
		int current_vi_index = 0;
		IndexPair& pair = index_pair_list[i];
		for (; current_poly < polygon_count; ++current_poly) 
		{
			const int vi_index = index_pair_list[current_poly].second;
			const int vertex_count = mesh_buffer.vertex_per_poly_buffer[vi_index];
			const int vertex_per_material = material_counter[current_mat];
			facecount_list_by_material.push_back(vertex_count);

			//if (static_cast<int>(vertex_list_by_material.size()) != vertex_per_material) {
			//	vertex_list_by_material.resize(vertex_per_material);
			//}

			for (int k = vertex_count- 1; k >= 0; --k) {
				const int viindex = start_vertex_number_list[vi_index] + k;
				const int vi = mesh_buffer.vertex_index_buffer[viindex];

				///// use cache
				//int abc_vi = 0;
				//if (is_first_mesh)
				//{
				//	if (fi_to_vi.find(vi) == fi_to_vi.end()) {
				//		fi_to_vi[vi] = current_vi_index;
				//		++current_vi_index;
				//	}
				//}
				//abc_vi = fi_to_vi[vi];

				const Imath::V3f& v = mesh_buffer.vertex_buffer[vi];
				vertex_list_by_material.push_back(v);
				face_list_by_material.push_back(current_vi_index);
				++current_vi_index;

				if (mesh_buffer.normal_index_buffer.empty()) {
					const Imath::V3f& n = mesh_buffer.normal_buffer[vi];
					normal_list_by_material.push_back(n);
				} else {
					const int ni = mesh_buffer.normal_index_buffer[viindex];
					const Imath::V3f& n = mesh_buffer.normal_buffer[ni];
					normal_list_by_material.push_back(n);
				}

				if (mesh_buffer.uv_index_buffer.empty()) {
					const Imath::V3f& uv = mesh_buffer.uv_buffer[vi];
					uv_list_by_material.push_back(Imath::V2f(uv.x, uv.y));
				} else {
					const int uvi = mesh_buffer.uv_index_buffer[viindex];
					const Imath::V3f& uv = mesh_buffer.uv_buffer[uvi];
					uv_list_by_material.push_back(Imath::V2f(uv.x, uv.y));
				}
			}

			if (index_pair_list[current_poly].first != current_mat) {
				current_mat = index_pair_list[current_poly].first;
				break;
			}
		}


		// store
		Alembic::AbcGeom::OPolyMeshSchema::Sample sample;

		// vertex
		Alembic::AbcGeom::P3fArraySample positions((const Imath::V3f *) &vertex_list_by_material.front(), vertex_list_by_material.size());
		sample.setPositions(positions);

		// face index
		if (is_first_mesh)
		{
			Alembic::Abc::Int32ArraySample face_indices(face_list_by_material);
			Alembic::Abc::Int32ArraySample face_counts(facecount_list_by_material);
			sample.setFaceIndices(face_indices);
			sample.setFaceCounts(face_counts);
		}

		// uv
		if (!uv_list_by_material.empty() && archive.is_export_uvs)
		{
			Alembic::AbcGeom::OV2fGeomParam::Sample uvSample;
			uvSample.setScope(Alembic::AbcGeom::kVertexScope);
			uvSample.setVals(Alembic::AbcGeom::V2fArraySample((const Imath::V2f *) &uv_list_by_material.front(), uv_list_by_material.size()));
			sample.setUVs(uvSample);
		}

		// normal
		if (!normal_list_by_material.empty() && archive.is_export_normals)
		{
			Alembic::AbcGeom::ON3fGeomParam::Sample normalSample;
			normalSample.setScope(Alembic::AbcGeom::kVertexScope);
			normalSample.setVals(Alembic::AbcGeom::N3fArraySample((const Alembic::AbcGeom::N3f *) &normal_list_by_material.front(), normal_list_by_material.size()));
			sample.setNormals(normalSample);
		}
		meshSchema.set(sample);
	}
}

static void start_alembic_export(const FunctionCallbackInfo<Value>& args)
{
	Isolate* isolate = Isolate::GetCurrent();
	HandleScope scope(isolate);

	if (args.Length() < 1) {
		isolate->ThrowException(Exception::TypeError(
			String::NewFromUtf8(isolate, "Wrong number of arguments")));
		return;
	}
	if (!args[0]->IsNumber()) {
		isolate->ThrowException(Exception::TypeError(
			String::NewFromUtf8(isolate, "Wrong arguments")));
		return;
	}
	const double fps = args[0]->NumberValue();

	if (!AlembicArchive::instance().archive)
	{
		std::string output_path("alembic_file.abc");
		AlembicArchive::instance().archive =
			new Alembic::Abc::OArchive(Alembic::AbcCoreHDF5::WriteArchive(), output_path.c_str());

		AlembicArchive &archive = AlembicArchive::instance();
		const double dt = 1.0 / fps;
		archive.timesampling = AbcA::TimeSamplingPtr(new AbcA::TimeSampling(dt, 0.0));
		archive.archive->addTimeSampling(*archive.timesampling);
		archive.is_export_normals = true;// (isExportNomals != 0);
		archive.is_export_uvs = true;// (is_export_uvs != 0);
		//archive.is_use_euler_rotation_camera = (is_use_euler_rotation_camera != 0);
		archive.export_mode = 0;// export_mode;
	}
}

static void end_alembic_export(const FunctionCallbackInfo<Value>& args)
{
	Isolate* isolate = Isolate::GetCurrent();
	HandleScope scope(isolate);

	AlembicArchive &archive = AlembicArchive::instance();
	if (archive.archive)
	{
		AlembicArchive::instance().end();
	}
}

static int to_int(char* buffer) {
	return *(reinterpret_cast<int*>(buffer));
}

static float to_float(char* buffer) {
	return *(reinterpret_cast<float*>(buffer));
}

static void export_alembic(const FunctionCallbackInfo<Value>& args)
{
	Isolate* isolate = Isolate::GetCurrent();
	HandleScope scope(isolate);

	if (args.Length() < 4) {
		isolate->ThrowException(Exception::TypeError(
			String::NewFromUtf8(isolate, "Wrong number of arguments")));
		return;
	}
	
	const int mesh_count = args[0]->Int32Value();

	char* mesh_sizes = node::Buffer::Data(args[1]);
	const int mesh_sizes_len = static_cast<int>(node::Buffer::Length(args[1]));

	char* mesh = node::Buffer::Data(args[2]);
	const int mesh_len = static_cast<int>(node::Buffer::Length(args[2]));

	char* mesh_index = node::Buffer::Data(args[3]);
	const int mesh_index_len =static_cast<int>(node::Buffer::Length(args[3]));

	MeshBuffer mbuffer;
	int mesh_pos = 0;
	int mesh_index_pos = 0;
	for (int i = 0; i < mesh_count; ++i) {
		const int points_size          = to_int(&mesh_sizes[64 * i + sizeof(uint64_t) * 0]);
		const int normals_size         = to_int(&mesh_sizes[64 * i + sizeof(uint64_t) * 1]);
		const int points_indices_size  = to_int(&mesh_sizes[64 * i + sizeof(uint64_t) * 2]);
		const int normals_indices_size = to_int(&mesh_sizes[64 * i + sizeof(uint64_t) * 3]);
		const int vert_per_poly_size   = to_int(&mesh_sizes[64 * i + sizeof(uint64_t) * 4]);
		const int uvs_size             = to_int(&mesh_sizes[64 * i + sizeof(uint64_t) * 5]);
		const int uv_indices_size      = to_int(&mesh_sizes[64 * i + sizeof(uint64_t) * 6]);
		const int shaders_count        = to_int(&mesh_sizes[64 * i + sizeof(uint64_t) * 7]);

		// vertex
		mbuffer.vertex_buffer.resize(points_size);
		for (int k = 0; k < points_size; ++k) {
			mbuffer.vertex_buffer[k].x = to_float(&mesh[mesh_pos]);
			mbuffer.vertex_buffer[k].y = to_float(&mesh[mesh_pos + 4]);
			mbuffer.vertex_buffer[k].z = to_float(&mesh[mesh_pos + 8]);
			mesh_pos += 12;
		}

		// normal
		mbuffer.normal_buffer.resize(normals_size);
		for (int k = 0; k < normals_size; ++k) {
			mbuffer.normal_buffer[k].x = to_float(&mesh[mesh_pos]);
			mbuffer.normal_buffer[k].y = to_float(&mesh[mesh_pos + 4]);
			mbuffer.normal_buffer[k].z = to_float(&mesh[mesh_pos + 8]);
			mesh_pos += 12;
		}

		// uv
		mbuffer.uv_buffer.resize(uvs_size);
		for (int k = 0; k < uvs_size; ++k) {
			mbuffer.uv_buffer[k].x = to_float(&mesh[mesh_pos]);
			mbuffer.uv_buffer[k].y = to_float(&mesh[mesh_pos + 4]);
			mbuffer.uv_buffer[k].z = to_float(&mesh[mesh_pos + 8]);
			mesh_pos += 12;
		}

		// vertex index
		mbuffer.vertex_index_buffer.resize(points_indices_size);
		for (int k = 0; k < points_indices_size; ++k) {
			mbuffer.vertex_index_buffer[k] = to_int(&mesh_index[mesh_index_pos]);
			mesh_index_pos += 4;
		}

		// vertex per poly index
		mbuffer.vertex_per_poly_buffer.resize(vert_per_poly_size);
		for (int k = 0; k < vert_per_poly_size; ++k) {
			mbuffer.vertex_per_poly_buffer[k] = to_int(&mesh_index[mesh_index_pos]);
			mesh_index_pos += 4;
		}

		// poly mat index
		mbuffer.poly_mat_buffer.resize(vert_per_poly_size);
		for (int k = 0; k < vert_per_poly_size; ++k) {
			mbuffer.poly_mat_buffer[k] = to_int(&mesh_index[mesh_index_pos]);
			mesh_index_pos += 4;
		}

		// normal index
		mbuffer.normal_index_buffer.resize(normals_indices_size);
		for (int k = 0; k < normals_indices_size; ++k) {
			mbuffer.normal_index_buffer[k] = to_int(&mesh_index[mesh_index_pos]);
			mesh_index_pos += 4;
		}

		// uv index
		mbuffer.uv_index_buffer.resize(uv_indices_size);
		for (int k = 0; k < uv_indices_size; ++k) {
			mbuffer.uv_index_buffer[k] = to_int(&mesh_index[mesh_index_pos]);
			mesh_index_pos += 4;
		}

		// export
		export_alembic_xform_by_material_fix_vindex(mbuffer, i);

		// subdiv_divider, general_vis
		mesh_pos += 8;
		// subdivide, camera visible, shadow visible
		mesh_index_pos += 12;

		mesh_pos = mesh_pos;
	}
}

void Init(Handle<Object> exports) {
	NODE_SET_METHOD(exports, "start_alembic_export", start_alembic_export);
	NODE_SET_METHOD(exports, "end_alembic_export", end_alembic_export);
	NODE_SET_METHOD(exports, "export_alembic", export_alembic);
}

NODE_MODULE(umnode, Init)
