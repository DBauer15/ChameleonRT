#include <algorithm>
#include <limits>
#include "util.h"
#include "dxr_utils.h"

using Microsoft::WRL::ComPtr;

bool dxr_available(ComPtr<ID3D12Device5> &device) {
	D3D12_FEATURE_DATA_D3D12_OPTIONS5 feature_data = { 0 };
	CHECK_ERR(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5,
		&feature_data, sizeof(feature_data)));

	return feature_data.RaytracingTier >= D3D12_RAYTRACING_TIER_1_0;
}

RootParam::RootParam(D3D12_ROOT_PARAMETER param, const std::string &name)
	: param(param), name(name)
{}

RootSignature::RootSignature(D3D12_ROOT_SIGNATURE_FLAGS flags, Microsoft::WRL::ComPtr<ID3D12RootSignature> sig,
	const std::vector<RootParam> &params)
	: flags(flags), sig(sig)
{
	size_t offset = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
	for (const auto &ip : params) {
		RootParam p = ip;
		p.offset = offset;
		if (p.param.ParameterType == D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS) {
			// Constants must pad to a size multiple of 8 to align w/ the pointer entries
			p.size = align_to(p.param.Constants.Num32BitValues * 4, sizeof(D3D12_GPU_DESCRIPTOR_HANDLE));
		} else {
			p.size = sizeof(D3D12_GPU_DESCRIPTOR_HANDLE);
		}
		param_offsets[p.name] = p;
		offset += p.size;

		std::cout << "Param: " << p.name << " is at offset "
			<< p.offset << ", size: " << p.size << "\n";
	}
}

size_t RootSignature::offset(const std::string &name) const {
	auto fnd = param_offsets.find(name);
	if (fnd != param_offsets.end()) {
		return fnd->second.offset;
	} else {
		return std::numeric_limits<size_t>::max();
	}
}

size_t RootSignature::size(const std::string &name) const {
	auto fnd = param_offsets.find(name);
	if (fnd != param_offsets.end()) {
		return fnd->second.size;
	}
	else {
		return std::numeric_limits<size_t>::max();
	}
}

size_t RootSignature::descriptor_table_offset() const {
	return offset("dxr_helper_desc_table");
}

size_t RootSignature::descriptor_table_size() const {
	// We know how big this will be, but it's just for convenience
	return 8;
}


ID3D12RootSignature* RootSignature::operator->() {
	return get();
}

ID3D12RootSignature* RootSignature::get() {
	return sig.Get();
}


RootSignatureBuilder RootSignatureBuilder::global() {
	RootSignatureBuilder sig;
	sig.flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
	return sig;
}
RootSignatureBuilder RootSignatureBuilder::local() {
	RootSignatureBuilder sig;
	sig.flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;
	return sig;
}

void RootSignatureBuilder::add_descriptor(D3D12_ROOT_PARAMETER_TYPE desc_type, const std::string &name,
	uint32_t shader_register, uint32_t space)
{
	D3D12_ROOT_PARAMETER p = { 0 };
	p.ParameterType = desc_type;
	p.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	p.Descriptor.ShaderRegister = shader_register;
	p.Descriptor.RegisterSpace = space;
	params.push_back(RootParam(p, name));
}

void RootSignatureBuilder::add_range(D3D12_DESCRIPTOR_RANGE_TYPE type,
	uint32_t size, uint32_t base_register, uint32_t space, uint32_t table_offset)
{
	D3D12_DESCRIPTOR_RANGE r = { 0 };
	r.RangeType = type;
	r.NumDescriptors = size;
	r.BaseShaderRegister = base_register;
	r.RegisterSpace = space;
	r.OffsetInDescriptorsFromTableStart = table_offset;
	ranges.push_back(r);
}

RootSignatureBuilder& RootSignatureBuilder::add_constants(const std::string &name, uint32_t shader_register,
	uint32_t space, uint32_t num_vals)
{
	D3D12_ROOT_PARAMETER p = { 0 };
	p.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
	p.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	p.Constants.ShaderRegister = shader_register;
	p.Constants.RegisterSpace = space;
	p.Constants.Num32BitValues = num_vals;
	params.push_back(RootParam(p, name));
	return *this;
}

RootSignatureBuilder& RootSignatureBuilder::add_srv(const std::string &name, uint32_t shader_register, uint32_t space) {
	add_descriptor(D3D12_ROOT_PARAMETER_TYPE_SRV, name, shader_register, space);
	return *this;
}
RootSignatureBuilder& RootSignatureBuilder::add_uav(const std::string &name, uint32_t shader_register, uint32_t space) {
	add_descriptor(D3D12_ROOT_PARAMETER_TYPE_UAV, name, shader_register, space);
	return *this;
}
RootSignatureBuilder& RootSignatureBuilder::add_cbv(const std::string &name, uint32_t shader_register, uint32_t space) {
	add_descriptor(D3D12_ROOT_PARAMETER_TYPE_CBV, name, shader_register, space);
	return *this;
}

RootSignatureBuilder& RootSignatureBuilder::add_srv_range(uint32_t size, uint32_t base_register,
	uint32_t space, uint32_t table_offset)
{
	add_range(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, size, base_register, space, table_offset);
	return *this;
}
RootSignatureBuilder& RootSignatureBuilder::add_uav_range(uint32_t size, uint32_t base_register,
	uint32_t space, uint32_t table_offset)
{
	add_range(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, size, base_register, space, table_offset);
	return *this;
}
RootSignatureBuilder& RootSignatureBuilder::add_cbv_range(uint32_t size, uint32_t base_register,
	uint32_t space, uint32_t table_offset)
{
	add_range(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, size, base_register, space, table_offset);
	return *this;
}
RootSignatureBuilder& RootSignatureBuilder::add_sampler_range(uint32_t size, uint32_t base_register,
	uint32_t space, uint32_t table_offset)
{
	add_range(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, size, base_register, space, table_offset);
	return *this;
}

RootSignature RootSignatureBuilder::create(ID3D12Device *device) {
	// Build the set of root parameters from the inputs
	// Pack constant values to the front, since we want to compact the shader record
	// to avoid a layout where we have something like the following:
	// [constant, pad]
	// [pointer]
	// [constant, pad]
	// since we could instead have done:
	// [constant, constant]
	// [pointer]
	// TODO WILL: Now I do need a name to associate with these params, since after I re-shuffle
	// them all around the order may not the one the add* calls were made, so the shader
	// record needs this info to setup the params properly
	std::stable_partition(params.begin(), params.end(),
		[](const RootParam &p) {
			return p.param.ParameterType == D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
	});

	if (!ranges.empty()) {
		// Append table the descriptor table parameter
		D3D12_ROOT_PARAMETER desc_table = { 0 };
		desc_table.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		desc_table.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		desc_table.DescriptorTable.NumDescriptorRanges = ranges.size();
		desc_table.DescriptorTable.pDescriptorRanges = ranges.data();
		params.push_back(RootParam(desc_table, "dxr_helper_desc_table"));
	}

	std::vector<D3D12_ROOT_PARAMETER> all_params;
	std::transform(params.begin(), params.end(), std::back_inserter(all_params),
		[](const RootParam &p) { return p.param; });

	D3D12_ROOT_SIGNATURE_DESC root_desc = { 0 };
	root_desc.NumParameters = all_params.size();
	root_desc.pParameters = all_params.data();
	root_desc.Flags = flags;

	// Create the root signature from the descriptor
	ComPtr<ID3DBlob> signature_blob;
	ComPtr<ID3DBlob> err_blob;
	auto res = D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1,
		&signature_blob, &err_blob);
	if (FAILED(res)) {
		std::cout << "Failed to serialize root signature: " << err_blob->GetBufferPointer() << "\n";
		throw std::runtime_error("Failed to serialize root signature");
	}

	ComPtr<ID3D12RootSignature> signature;
	CHECK_ERR(device->CreateRootSignature(0, signature_blob->GetBufferPointer(),
		signature_blob->GetBufferSize(), IID_PPV_ARGS(&signature)));

	return RootSignature(flags, signature, params);
}

ShaderLibrary::ShaderLibrary(const void *code, const size_t code_size,
	const std::vector<std::wstring> &export_fns)
	: export_functions(export_fns)
{
	bytecode.pShaderBytecode = code;
	bytecode.BytecodeLength = code_size;
	build_library_desc();
}

ShaderLibrary::ShaderLibrary(const ShaderLibrary &other)
	: bytecode(other.bytecode), export_functions(other.export_functions)
{
	build_library_desc();
}

ShaderLibrary& ShaderLibrary::operator=(const ShaderLibrary &other) {
	bytecode = other.bytecode;
	export_functions = other.export_functions;
	build_library_desc();
	return *this;
}

const std::vector<std::wstring>& ShaderLibrary::export_names() const {
	return export_functions;
}

size_t ShaderLibrary::num_exports() const {
	return export_fcn_ptrs.size();
}

LPCWSTR* ShaderLibrary::export_names_ptr() {
	return export_fcn_ptrs.data();
}

LPCWSTR* ShaderLibrary::find_export(const std::wstring &name) {
	auto fnd = std::find(export_functions.begin(), export_functions.end(), name);
	if (fnd != export_functions.end()) {
		size_t idx = std::distance(export_functions.begin(), fnd);
		return &export_fcn_ptrs[idx];
	} else {
		return nullptr;
	}
}

const D3D12_DXIL_LIBRARY_DESC* ShaderLibrary::library() const {
	return &slibrary;
}

void ShaderLibrary::build_library_desc() {
	for (const auto &fn : export_functions) {
		D3D12_EXPORT_DESC shader_export = { 0 };
		shader_export.ExportToRename = nullptr;
		shader_export.Flags = D3D12_EXPORT_FLAG_NONE;
		shader_export.Name = fn.c_str();
		exports.push_back(shader_export);
		export_fcn_ptrs.push_back(fn.c_str());
	}
	slibrary.DXILLibrary = bytecode;
	slibrary.NumExports = exports.size();
	slibrary.pExports = exports.data();
}

RootSignatureAssociation::RootSignatureAssociation(const std::vector<std::wstring> &functions,
	const RootSignature &signature)
	: functions(functions), signature(signature)
{}

HitGroup::HitGroup(const std::wstring &name, D3D12_HIT_GROUP_TYPE type,
	const std::wstring &closest_hit, const std::wstring &any_hit,
	const std::wstring &intersection)
	: name(name), closest_hit(closest_hit), type(type), any_hit(any_hit), intersection(intersection)
{}

bool HitGroup::has_any_hit() const {
	return !any_hit.empty();
}
bool HitGroup::has_intersection() const {
	return !intersection.empty();
}

ShaderPayloadConfig::ShaderPayloadConfig(const std::vector<std::wstring> &functions,
	uint32_t max_payload_size, uint32_t max_attrib_size)
	: functions(functions)
{
	desc.MaxPayloadSizeInBytes = max_payload_size;
	desc.MaxAttributeSizeInBytes = max_attrib_size;
}

bool RTPipelineBuilder::has_global_root_sig() const {
	return global_sig.sig.Get() != nullptr;
}

RTPipelineBuilder& RTPipelineBuilder::add_shader_library(const ShaderLibrary &library) {
	shader_libs.push_back(library);
	return *this;
}

RTPipelineBuilder& RTPipelineBuilder::set_ray_gen(const std::wstring &rg) {
	assert(ray_gen.empty());
	ray_gen = rg;
	return *this;
}

RTPipelineBuilder& RTPipelineBuilder::add_miss_shader(const std::wstring &miss_fn) {
	assert(miss_shaders.empty());
	miss_shaders.push_back(miss_fn);
	return *this;
}

RTPipelineBuilder& RTPipelineBuilder::add_miss_shader(const std::vector<std::wstring> &miss_fn) {
	assert(miss_shaders.empty());
	miss_shaders = miss_fn;
	return *this;
}

RTPipelineBuilder& RTPipelineBuilder::add_hit_group(const HitGroup &hg) {
	hit_groups.push_back({ hg });
	return *this;
}

RTPipelineBuilder& RTPipelineBuilder::add_hit_group(const std::vector<HitGroup> &hg) {
	hit_groups.push_back(hg);
	return *this;
}

RTPipelineBuilder& RTPipelineBuilder::configure_shader_payload(const std::vector<std::wstring> &functions,
	uint32_t max_payload_size, uint32_t max_attrib_size)
{
	payload_configs.emplace_back(functions, max_payload_size, max_attrib_size);
	return *this;
}

RTPipelineBuilder& RTPipelineBuilder::set_max_recursion(uint32_t depth) {
	recursion_depth = depth;
	return *this;
}

RTPipelineBuilder& RTPipelineBuilder::set_shader_root_sig(const std::vector<std::wstring> &functions,
	const RootSignature &sig)
{
	signature_associations.emplace_back(functions, sig);
	return *this;
}

RTPipelineBuilder& RTPipelineBuilder::set_global_root_sig(const RootSignature &sig) {
	global_sig = sig;
	return *this;
}

RTPipeline RTPipelineBuilder::create(ID3D12Device5 *device) {
	if (ray_gen.empty()) {
		throw std::runtime_error("No ray generation shader set!");
	}

	size_t num_ray_types = 0;
	if (!hit_groups.empty()) {
		num_ray_types = hit_groups[0].size();
		for (const auto &hg : hit_groups) {
			if (num_ray_types != hg.size()) {
				throw std::runtime_error("HitGroup does not have shaders for all ray types");
			}
		}
	}

	if (!miss_shaders.empty() && miss_shaders.size() != num_ray_types) {
		throw std::runtime_error("Miss Shaders are not specified for each ray type");
	}

	if (num_ray_types >= 256) {
		throw std::runtime_error("Too many ray types: Max is 255");
	}

	size_t num_association_subobjs = 0;
	size_t num_associated_fcns = 0;
	const size_t total_subobjs = compute_num_subobjects(num_association_subobjs, num_associated_fcns);
	
	std::vector<D3D12_STATE_SUBOBJECT> subobjects;
	subobjects.resize(total_subobjs);
	size_t current_obj = 0;

	std::vector<D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION> associations;
	associations.resize(num_association_subobjs);
	size_t current_assoc = 0;

	std::vector<LPCWSTR> associated_fcns;
	associated_fcns.resize(num_associated_fcns, nullptr);
	size_t current_assoc_fcn = 0;

	// Add the shader libraries
	for (const auto &lib : shader_libs) {
		D3D12_STATE_SUBOBJECT l = { 0 };
		l.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
		l.pDesc = lib.library();
		subobjects[current_obj++] = l;
	}

	// Make the hit group descriptors for each hit group and ray type and add them
	std::vector<D3D12_HIT_GROUP_DESC> hg_descs;
	if (!hit_groups.empty()) {
		hg_descs.resize(hit_groups.size() * num_ray_types);
		size_t current_hg = 0;
		for (const auto &hg : hit_groups) {
			for (const auto &g : hg) {
				D3D12_HIT_GROUP_DESC &desc = hg_descs[current_hg++];
				desc.HitGroupExport = g.name.c_str();
				desc.Type = g.type;
				desc.ClosestHitShaderImport = g.closest_hit.c_str();
				desc.IntersectionShaderImport = g.has_intersection() ? g.intersection.c_str() : nullptr;
				desc.AnyHitShaderImport = g.has_any_hit() ? g.any_hit.c_str() : nullptr;

				D3D12_STATE_SUBOBJECT o = { 0 };
				o.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
				o.pDesc = &desc;
				subobjects[current_obj++] = o;
			}
		}
	}

	// Make the shader payload configs and associate them with the desired functions
	for (const auto &c : payload_configs) {
		// Add the shader config object
		D3D12_STATE_SUBOBJECT o = { 0 };
		o.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
		o.pDesc = &c.desc;
		subobjects[current_obj++] = o;

		// Associate it with the exports
		D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION &assoc = associations[current_assoc++];
		assoc.NumExports = c.functions.size();
		assoc.pExports = &associated_fcns[current_assoc_fcn];
		assoc.pSubobjectToAssociate = &subobjects[current_obj - 1];

		// Copy over the names referenced by this association
		for (const auto &name : c.functions) {
			associated_fcns[current_assoc_fcn++] = name.c_str();
		}

		D3D12_STATE_SUBOBJECT payload_subobj = { 0 };
		payload_subobj.Type = D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION;
		payload_subobj.pDesc = &assoc;
		subobjects[current_obj++] = payload_subobj;
	}

	// Make the local root signature objects and associations
	std::vector<D3D12_LOCAL_ROOT_SIGNATURE> local_root_sigs;
	if (!signature_associations.empty()) {
		local_root_sigs.resize(signature_associations.size());
		size_t current_sig = 0;
		for (auto &sig : signature_associations) {
			// Add the local root signature
			D3D12_LOCAL_ROOT_SIGNATURE &local_sig = local_root_sigs[current_sig++];
			local_sig.pLocalRootSignature = sig.signature.get();

			D3D12_STATE_SUBOBJECT root_sig_obj = { 0 };
			root_sig_obj.Type = D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE;
			root_sig_obj.pDesc = &local_sig;
			subobjects[current_obj++] = root_sig_obj;

			// Associate it with the exports
			D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION &assoc = associations[current_assoc++];
			assoc.NumExports = sig.functions.size();
			assoc.pExports = &associated_fcns[current_assoc_fcn];
			assoc.pSubobjectToAssociate = &subobjects[current_obj - 1];

			// Copy over the names referenced by this association
			for (const auto &name : sig.functions) {
				associated_fcns[current_assoc_fcn++] = name.c_str();
			}

			D3D12_STATE_SUBOBJECT payload_subobj = { 0 };
			payload_subobj.Type = D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION;
			payload_subobj.pDesc = &assoc;
			subobjects[current_obj++] = payload_subobj;
		}
	}

	// Add the raytracing pipeline config
	D3D12_RAYTRACING_PIPELINE_CONFIG pipeline_cfg = { 0 };
	pipeline_cfg.MaxTraceRecursionDepth = 1;
	{
		D3D12_STATE_SUBOBJECT o = { 0 };
		o.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG;
		o.pDesc = &pipeline_cfg;
		// 8: Pipeline config
		subobjects[current_obj++] = o;
	}

	// Add the global root signature if we have one
	D3D12_GLOBAL_ROOT_SIGNATURE global_root_sig_obj = { 0 };
	if (has_global_root_sig()) {
		global_root_sig_obj.pGlobalRootSignature = global_sig.get();
		D3D12_STATE_SUBOBJECT o = { 0 };
		o.Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE;
		o.pDesc = &global_root_sig_obj;
		subobjects[current_obj++] = o;
	}

	D3D12_STATE_OBJECT_DESC pipeline_desc = { 0 };
	pipeline_desc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
	pipeline_desc.NumSubobjects = current_obj;
	pipeline_desc.pSubobjects = subobjects.data();

	RTPipeline pipeline;
	pipeline.global_sig = global_sig;
	CHECK_ERR(device->CreateStateObject(&pipeline_desc, IID_PPV_ARGS(&pipeline.state)));
	return pipeline;
}

size_t RTPipelineBuilder::compute_num_subobjects(size_t &num_export_associations, size_t &num_associated_fcns) const {
	// Compute how many state objects we'll need for this pipeline
	// Each DXIL library takes one subobject
	size_t num_subobjs = shader_libs.size();

	// Each hit group takes one subobject
	for (const auto &hg : hit_groups) {
		num_subobjs += hg.size();
	}

	// Each shader payload config takes two subobjects:
	// One to declare the config, and another to associate it with the functions
	num_subobjs += payload_configs.size() * 2;
	num_export_associations = payload_configs.size();
	num_associated_fcns = 0;
	for (const auto &c : payload_configs) {
		num_associated_fcns += c.functions.size();
	}

	// Each local root signature association takes two subobjects:
	// One to declare the subobject, and another to associate it with the functions
	num_subobjs += signature_associations.size() * 2;
	num_export_associations += signature_associations.size();
	for (const auto &a : signature_associations) {
		num_associated_fcns += a.functions.size();
	}

	// Specifying the max trace recursion depth takes 1 subobject
	++num_subobjs;

	// If we have a global root signature that takes 1 subobject
	if (has_global_root_sig()) {
		++num_subobjs;
	}
	std::cout << "Pipeline requires " << num_subobjs << " subobjects\n"
		<< "Has " << num_export_associations << " export assocs\n"
		<< "With " << num_associated_fcns << " associated fcns\n";
	return num_subobjs;
}


bool RTPipeline::has_global_root_sig() const {
	return global_sig.sig.Get() != nullptr;
}

ID3D12StateObject* RTPipeline::operator->() {
	return get();
}
ID3D12StateObject* RTPipeline::get() {
	return state.Get();
}
