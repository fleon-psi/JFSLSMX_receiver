#include <string>
#include <iostream>
#include <fstream>

#include <hdf5.h>
//#include <filesystem>

#include "../bitshuffle/bitshuffle.h"
#include "../bitshuffle/bshuf_h5filter.h"

#include "JFWriter.h"

#define SENSOR_THICKNESS_IN_UM 320.0
#define PIXEL_SIZE_IN_UM        75.0
#define PIXEL_SIZE_IN_MM       (PIXEL_SIZE_IN_UM/1000.0)
#define DETECTOR_NAME          "JF4M"
#define SOURCE_NAME            "SLS"
#define INSTRUMENT_NAME        "X06DA"

#define HDF5_ERROR(ret,func) if (ret) printf("%s(%d) %s: err = %d\n",__FILE__,__LINE__, #func, ret), exit(ret)

hid_t *data_hdf5;
hid_t *data_hdf5_group;
hid_t *data_hdf5_dataset;
hid_t *data_hdf5_dcpl;
hid_t *data_hdf5_dataspace;

pthread_mutex_t hdf5_mutex = PTHREAD_MUTEX_INITIALIZER;

void make_dirs(std::string path) {
    int pos = path.rfind("/");
    if (pos != std::string::npos) {
        std::cout << "Making directory " <<path.substr(0,pos) << std::endl;
//        std::filesystem::create_directories(path.substr(0,pos));
    }
}

int createDataChunkLink(std::string filename, hid_t location, std::string name, std::string remote) {
    std::string pure_filename;
    int pos = filename.rfind("/");
    if (pos != std::string::npos) {
        herr_t status = H5Lcreate_external(filename.substr(pos+1).c_str(), remote.c_str(),location,name.c_str(), H5P_DEFAULT, H5P_DEFAULT);
    } else {
        herr_t status = H5Lcreate_external(filename.c_str(), remote.c_str() ,location,name.c_str(), H5P_DEFAULT, H5P_DEFAULT);
    }
    return 0;
}

int addStringAttribute(hid_t location, std::string name, std::string val) {
	/* https://support.hdfgroup.org/ftp/HDF5/current/src/unpacked/examples/h5_attribute.c */
	hid_t aid = H5Screate(H5S_SCALAR);
	hid_t atype = H5Tcopy(H5T_C_S1);
	H5Tset_size(atype, val.length());
	H5Tset_strpad(atype,H5T_STR_NULLTERM);
	hid_t attr = H5Acreate2(location, name.c_str(), atype, aid, H5P_DEFAULT, H5P_DEFAULT);
	herr_t ret = H5Awrite(attr, atype, val.c_str());
	ret = H5Sclose(aid);
	ret = H5Tclose(atype);
	ret = H5Aclose(attr);

	return 0;
}

int addDoubleAttribute(hid_t location, std::string name, const double *val, int dim) {
    // https://support.hdfgroup.org/ftp/HDF5/current/src/unpacked/examples/h5_crtdat.c
    hsize_t dims[1];
    dims[0] = dim;

    /* Create the data space for the dataset. */
    hid_t dataspace_id = H5Screate_simple(1, dims, NULL);

    hid_t attr = H5Acreate2(location, name.c_str(), H5T_IEEE_F64LE, dataspace_id, H5P_DEFAULT, H5P_DEFAULT);

    herr_t ret = H5Awrite(attr, H5T_NATIVE_DOUBLE, val);

    ret = H5Sclose(dataspace_id);
    ret = H5Aclose(attr);

    return 0;
}

hid_t createGroup(hid_t master_file_id, std::string group, std::string nxattr) {
	hid_t group_id = H5Gcreate(master_file_id, group.c_str(), H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
	if (nxattr != "") addStringAttribute(group_id, "NX_class", nxattr);
	return group_id;
}

// +Compress
int saveUInt16_3D(hid_t location, std::string name, const uint16_t *val, int dim1, int dim2, int dim3, double multiplier) {
    herr_t status;

    // https://support.hdfgroup.org/ftp/HDF5/current/src/unpacked/examples/h5_crtdat.c
    hsize_t dims[3];
    dims[0] = dim1;
    dims[1] = dim2;
    dims[2] = dim3;

    // Create the data space for the dataset.
    hid_t dataspace_id = H5Screate_simple(3, dims, NULL);

    // Create data property
    hid_t dcpl_id = H5Pcreate(H5P_DATASET_CREATE);
    hsize_t chunk_size[3] = {dim1, dim2, dim3};
    H5Pset_chunk(dcpl_id, 3, chunk_size);

    // set filter
    unsigned int params[] = {LZ4_BLOCK_SIZE, BSHUF_H5_COMPRESS_LZ4};                
    status = H5Pset_filter(dcpl_id, (H5Z_filter_t)BSHUF_H5FILTER, H5Z_FLAG_MANDATORY, (size_t)2, params);
    HDF5_ERROR(status,H5Pset_filter);

    // Create the dataset.
    hid_t dataset_id = H5Dcreate2(location, name.c_str(), H5T_STD_U16LE, dataspace_id,
                                  H5P_DEFAULT, dcpl_id, H5P_DEFAULT);

    // Write the dataset.
    status = H5Dwrite(dataset_id, H5T_NATIVE_USHORT, H5S_ALL, H5S_ALL, H5P_DEFAULT,
                      val);
    HDF5_ERROR(status,H5Dwrite);

    addDoubleAttribute(dataset_id, "multiplier", &multiplier, 1);

    // End access to the dataset and release resources used by it.
    status = H5Dclose(dataset_id);

    status = H5Pclose(dcpl_id);

    // Terminate access to the data space.
    status = H5Sclose(dataspace_id);

    return 0;
}

int saveString1D(hid_t location, std::string name, char *val, std::string units, int dim, int len) {
    herr_t status;

    // https://support.hdfgroup.org/ftp/HDF5/current/src/unpacked/examples/h5_crtdat.c
    hsize_t dims[1];
    dims[0] = dim;

    /* Create the data space for the dataset. */
    hid_t dataspace_id = H5Screate_simple(1, dims, NULL);

    hid_t atype = H5Tcopy(H5T_C_S1);
    H5Tset_size(atype, len);
    H5Tset_strpad(atype,H5T_STR_NULLTERM);
    /* Create the dataset. */
    hid_t dataset_id = H5Dcreate2(location, name.c_str(), atype, dataspace_id,
                                  H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);

    /* Write the dataset. */
    status = H5Dwrite(dataset_id, atype, H5S_ALL, H5S_ALL, H5P_DEFAULT,
                      val);
    if (units != "") addStringAttribute(dataset_id, "units", units);

    /* End access to the dataset and release resources used by it. */
    status = H5Dclose(dataset_id);

    /* Terminate access to the data space. */
    status = H5Sclose(dataspace_id);
    return 0;
}

int saveDouble1D(hid_t location, std::string name, const double *val, std::string units, int dim) {
    herr_t status;
    
    // https://support.hdfgroup.org/ftp/HDF5/current/src/unpacked/examples/h5_crtdat.c
    hsize_t dims[1];
    dims[0] = dim;
    
    /* Create the data space for the dataset. */
    hid_t dataspace_id = H5Screate_simple(1, dims, NULL);
    
    /* Create the dataset. */
    hid_t dataset_id = H5Dcreate2(location, name.c_str(), H5T_IEEE_F32LE, dataspace_id,
                                  H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    
    /* Write the dataset. */
    status = H5Dwrite(dataset_id, H5T_NATIVE_DOUBLE, H5S_ALL, H5S_ALL, H5P_DEFAULT,
                      val);
    
    if (units != "") addStringAttribute(dataset_id, "units", units);
    
    /* End access to the dataset and release resources used by it. */
    status = H5Dclose(dataset_id);
    
    /* Terminate access to the data space. */
    status = H5Sclose(dataspace_id);
    
    return 0;
}

int saveUInt2D(hid_t location, std::string name, const uint32_t *val, std::string units, int dim1, int dim2) {
    herr_t status;
    
    // https://support.hdfgroup.org/ftp/HDF5/current/src/unpacked/examples/h5_crtdat.c
    hsize_t dims[2];
    dims[0] = dim1;
    dims[1] = dim2;
    
    /* Create the data space for the dataset. */
    hid_t dataspace_id = H5Screate_simple(2, dims, NULL);
    
    /* Create the dataset. */
    hid_t dataset_id = H5Dcreate2(location, name.c_str(), H5T_STD_U32LE, dataspace_id,
                                  H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    
    /* Write the dataset. */
    status = H5Dwrite(dataset_id, H5T_NATIVE_UINT, H5S_ALL, H5S_ALL, H5P_DEFAULT,
                      val);
    
    if (units != "") addStringAttribute(dataset_id, "units", units);
    
    /* End access to the dataset and release resources used by it. */
    status = H5Dclose(dataset_id);
    
    /* Terminate access to the data space. */
    status = H5Sclose(dataspace_id);
    
    return 0;
}

int saveUInt16_as_32_2D(hid_t location, std::string name, const uint16_t *val, std::string units, int dim1, int dim2) {
    herr_t status;
    
    // https://support.hdfgroup.org/ftp/HDF5/current/src/unpacked/examples/h5_crtdat.c
    hsize_t dims[2];
    dims[0] = dim1;
    dims[1] = dim2;
    
    /* Create the data space for the dataset. */
    hid_t dataspace_id = H5Screate_simple(2, dims, NULL);
    
    /* Create the dataset. */
    hid_t dataset_id = H5Dcreate2(location, name.c_str(), H5T_STD_U32LE, dataspace_id,
                                  H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    
    /* Write the dataset. */
    status = H5Dwrite(dataset_id, H5T_STD_U16LE, H5S_ALL, H5S_ALL, H5P_DEFAULT,
                      val);
    
    if (units != "") addStringAttribute(dataset_id, "units", units);
    
    /* End access to the dataset and release resources used by it. */
    status = H5Dclose(dataset_id);
    
    /* Terminate access to the data space. */
    status = H5Sclose(dataspace_id);
    
    return 0;
}

int saveInt1D(hid_t location, std::string name, const int *val, std::string units, int dim) {
    herr_t status;
    
    // https://support.hdfgroup.org/ftp/HDF5/current/src/unpacked/examples/h5_crtdat.c
    hsize_t dims[1];
    dims[0] = dim;
    
    /* Create the data space for the dataset. */
    hid_t dataspace_id = H5Screate_simple(1, dims, NULL);
    
    /* Create the dataset. */
    hid_t dataset_id = H5Dcreate2(location, name.c_str(), H5T_STD_I32LE, dataspace_id,
                                  H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    
    /* Write the dataset. */
    status = H5Dwrite(dataset_id, H5T_NATIVE_INT, H5S_ALL, H5S_ALL, H5P_DEFAULT,
                      val);
    
    if (units != "") addStringAttribute(dataset_id, "units", units);
    
    /* End access to the dataset and release resources used by it. */
    status = H5Dclose(dataset_id);
    
    /* Terminate access to the data space. */
    status = H5Sclose(dataspace_id);
    
    return 0;
}

int saveString(hid_t location, std::string name, std::string val, std::string units = "") {
    herr_t status;
    
    // https://support.hdfgroup.org/ftp/HDF5/current/src/unpacked/examples/h5_crtdat.c
    hsize_t dims[1];
    dims[0] = 1;
    
    /* Create the data space for the dataset. */
    hid_t dataspace_id = H5Screate_simple(1, dims, NULL);
    
    hid_t atype = H5Tcopy(H5T_C_S1);
    H5Tset_size(atype, val.length()+1);
    H5Tset_strpad(atype,H5T_STR_NULLTERM);
    /* Create the dataset. */
    hid_t dataset_id = H5Dcreate2(location, name.c_str(), atype, dataspace_id,
                                  H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    
    /* Write the dataset. */
    status = H5Dwrite(dataset_id, atype, H5S_ALL, H5S_ALL, H5P_DEFAULT,
                      val.c_str());
    if (units != "") addStringAttribute(dataset_id, "units", units);
    
    /* End access to the dataset and release resources used by it. */
    status = H5Dclose(dataset_id);
    
    /* Terminate access to the data space. */
    status = H5Sclose(dataspace_id);
    return 0;
}

int saveTimeUTC(hid_t location, std::string name, time_t time) {
    char buf[255];
    strftime(buf, sizeof(buf), "%FT%TZ", gmtime(&time));
    return saveString(location, name, std::string(buf));
}

int saveDouble(hid_t location, std::string name, double val, std::string units = "") {
    double tmp = val;
    return saveDouble1D(location, name, &tmp, units, 1);
}

int saveInt(hid_t location, std::string name, int val, std::string units = "") {
    int tmp = val;
    return saveInt1D(location, name, &tmp, units, 1);
}

void transform_and_write_mask(hid_t grp) {
   uint32_t *pixel_mask = (uint32_t *) calloc(XPIXEL * YPIXEL, sizeof(uint32_t));

   for (int module = 0; module < NMODULES*NCARDS; module ++) {
       for (uint64_t line = 0; line < MODULE_LINES; line ++) {
           size_t pixel_in = (module * MODULE_LINES + line) * MODULE_COLS;
           size_t line_out = (513L - line - (line / 256) * 2L + 514L * (3 - (module / 2))) * 2L + module % 2;
           // Modules are interleaved to make 2 modules in one line
           for (uint64_t column = 0; column < MODULE_COLS; column ++) {
               size_t pixel_out = line_out * (XPIXEL/2) + column + (column / 256) * 2L;
               pixel_mask[pixel_out] = gain_pedestal.pixel_mask[pixel_in + column];
               // Vertical multipixel lines
               if ((column % 256 == 0) && (column != 0)) pixel_mask[pixel_out - 1] = pixel_mask[pixel_out];
               if ((column % 256 == 255) && (column != MODULE_COLS)) pixel_mask[pixel_out + 1] = pixel_mask[pixel_out];           
           }
       }
   }

   // Copy horizontal multi-pixel lines
   for (int i = 0; i < NMODULES / 2; i ++) {
       memcpy(pixel_mask + (514 * i + 256) * 1030 * 2, pixel_mask + (514 * i + 255) * 1030 * 2, 2 * 1030 * sizeof(uint32_t));
       memcpy(pixel_mask + (514 * i + 257) * 1030 * 2, pixel_mask + (514 * i + 258) * 1030 * 2, 2 * 1030 * sizeof(uint32_t));
   }

   saveUInt2D(grp, "pixel_mask", pixel_mask, "", YPIXEL, XPIXEL);
   free(pixel_mask);
}

void write_metrology(hid_t master_file_id) {
        double moduleOrigin[NMODULES*NCARDS][3];
	double detectorCenter[3] = {0.0, 0.0, 0.0};

	for (int i = 0; i < NMODULES*NCARDS; i ++) {
                double corner_x = (i/2) * (514 + 36); // +GAP
                double corner_y = (i%2) * (1030 + 8); // +GAP
		// 1. Find module corner in lab coordinates, based on pixel coordinates
		moduleOrigin[i][0] = (corner_x - experiment_settings.beam_x) * PIXEL_SIZE_IN_MM;
		moduleOrigin[i][1] = (corner_y - experiment_settings.beam_y) * PIXEL_SIZE_IN_MM;
		moduleOrigin[i][2] = 0.0; // No Z offset for modules at the moment

		// 2. Calculate vector from module in mm
		double moduleCenter[3];
		moduleCenter[0] = moduleOrigin[i][0] + PIXEL_SIZE_IN_MM * 1030.0 / 2.0;
		moduleCenter[1] = moduleOrigin[i][1] + PIXEL_SIZE_IN_MM * 514.0 / 2.0;
		moduleCenter[2] = moduleOrigin[i][2];

		// 3. Find detector center
		detectorCenter[0] += moduleCenter[0];
		detectorCenter[1] += moduleCenter[1];
		detectorCenter[2] += moduleCenter[2];
	}

	detectorCenter[0] /= NMODULES*NCARDS;
	detectorCenter[1] /= NMODULES*NCARDS;
	detectorCenter[2] /= NMODULES*NCARDS;

	hid_t grp, dataset;

	grp = createGroup(master_file_id, "/entry/instrument/" DETECTOR_NAME "/transformations","NXtransformations");

	saveDouble(grp, "AXIS_RAIL", experiment_settings.detector_distance, "mm");

	double rail_vector[3] = {0,0,1};

	dataset = H5Dopen2(grp, "AXIS_RAIL", H5P_DEFAULT);
	addStringAttribute(dataset, "depends_on", ".");
	addStringAttribute(dataset, "equipment", "detector");
	addStringAttribute(dataset, "equipment_component", "detector_arm");
	addStringAttribute(dataset, "transformation_type", "translation");
	addDoubleAttribute(dataset, "vector", rail_vector, 3);
	H5Dclose(dataset);

	saveDouble(grp, "AXIS_D0", 0.0, "degrees");

	double d0_vector[3] = {0, 0, 1};

	dataset = H5Dopen2(grp , "AXIS_D0", H5P_DEFAULT);
	addStringAttribute(dataset, "depends_on", "AXIS_RAIL");
	addStringAttribute(dataset, "equipment", "detector");
	addStringAttribute(dataset, "equipment_component", "detector_arm");
	addStringAttribute(dataset, "transformation_type", "rotation");
	addDoubleAttribute(dataset, "vector", d0_vector, 3);
	addDoubleAttribute(dataset, "offset", detectorCenter, 3);
	addStringAttribute(dataset, "offset_units", "mm");
	H5Dclose(dataset);

	for (int i = 0; i < NMODULES*NCARDS; i++) {
		double mod_vector[3] = {0, 0, 1};
		double mod_offset[3] = {moduleOrigin[i][0] - detectorCenter[0], moduleOrigin[i][1] - detectorCenter[1], moduleOrigin[i][2] - detectorCenter[2]};
		std::string detModuleAxis = "AXIS_D0M" + std::to_string(i);
		saveDouble(grp, detModuleAxis, 0.0, "degrees");
		dataset = H5Dopen2(grp , detModuleAxis.c_str(), H5P_DEFAULT);
		addStringAttribute(dataset, "depends_on", "AXIS_D0");
		addStringAttribute(dataset, "equipment", "detector");
		addStringAttribute(dataset, "equipment_component", "detector_module");
		addStringAttribute(dataset, "transformation_type", "rotation");
		addDoubleAttribute(dataset, "vector", mod_vector, 3);
		addDoubleAttribute(dataset, "offset", mod_offset, 3);
		addStringAttribute(dataset, "offset_units", "mm");
		H5Dclose(dataset);
	}

	H5Gclose(grp);

	for (int i = 0; i < NMODULES*NCARDS; i++) {
		std::string moduleGroup = "/entry/instrument/detector/ARRAY_D0M" + std::to_string(i);
		grp = createGroup(master_file_id, moduleGroup.c_str() ,"NXdetector_module");
		int origin[2] = {(i % 2) * 514, (i / 2 ) *1030};
		int size[2] = {514,1030};
		saveInt1D(grp, "data_origin", origin, "", 2);
		saveInt1D(grp, "data_size", size, "", 2);

		saveDouble(grp, "fast_pixel_direction", PIXEL_SIZE_IN_MM, "mm");

		double offset_fast[3] = {0,0,0};

		dataset = H5Dopen2(grp , "fast_pixel_direction", H5P_DEFAULT);
		addStringAttribute(dataset, "transformation_type","translation");
		addStringAttribute(dataset, "depends_on","/entry/instrument/" DETECTOR_NAME "/transformations/AXIS_D0M" + std::to_string(i));
		addDoubleAttribute(dataset, "offset", offset_fast, 3);
		
                double vector_fast[3] = {1,0,0};
		addDoubleAttribute(dataset, "vector", vector_fast, 3);

		H5Dclose(dataset);

		saveDouble(grp, "slow_pixel_direction", PIXEL_SIZE_IN_MM, "mm");

		double offset_slow[3] = {0,0,0};

		dataset = H5Dopen2(grp , "slow_pixel_direction", H5P_DEFAULT);
		addStringAttribute(dataset, "transformation_type","translation");
		addStringAttribute(dataset, "depends_on","/entry/instrument/" DETECTOR_NAME "/transformations/AXIS_D0M" + std::to_string(i));
		addDoubleAttribute(dataset, "offset", offset_slow, 3);
		double vector_slow[3] = {0,1,0};
		addDoubleAttribute(dataset, "vector", vector_slow, 3);

		H5Dclose(dataset);

		H5Gclose(grp);
	}
}

int save_master_hdf5() {
	std::string filename = "";
	if (writer_settings.main_location != "") { 
             make_dirs(writer_settings.main_location + "/" + writer_settings.HDF5_prefix);
             filename =
			writer_settings.main_location + "/" +
			writer_settings.HDF5_prefix + "_master.h5";
        }
	else filename = writer_settings.HDF5_prefix + "_master.h5";

	// Create Master file
	hid_t hdf5_file = H5Fcreate(filename.c_str(), H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
	if (hdf5_file < 0) {
		std::cerr << "Cannot create file: " << filename << std::endl;
		return 1;
	}
	hid_t grp = createGroup(hdf5_file, "/entry", "NXentry");
        saveString(grp,"definition", "NXmx");
        saveTimeUTC(grp, "start_time", time_datacollection);
        saveTimeUTC(grp, "end_time_esimated", time_datacollection + (time_t) (experiment_settings.nframes_to_collect * experiment_settings.frame_time));
	H5Gclose(grp);

	grp = createGroup(hdf5_file, "/entry/data","NXdata");

        int data_hdf5_files = experiment_settings.nframes_to_write / writer_settings.images_per_file;
        if (experiment_settings.nframes_to_write % writer_settings.images_per_file > 0) data_hdf5_files++;
        for (int i = 0; i < data_hdf5_files; i++) {
            char buff[12];
	    snprintf(buff,12,"data_%06d",i+1);
	    std::string filename = writer_settings.HDF5_prefix + "_" + std::string(buff) + ".h5";
            createDataChunkLink(filename, grp, std::string(buff), "/entry/data/data");
        }
	H5Gclose(grp);

	grp = createGroup(hdf5_file, "/entry/source","NXsource");
        saveString(grp, "name", SOURCE_NAME);        
	H5Gclose(grp);

	grp = createGroup(hdf5_file, "/entry/instrument","NXinstrument");
        saveString(grp, "name", INSTRUMENT_NAME);        
	H5Gclose(grp);

	grp = createGroup(hdf5_file, "/entry/instrument/filter", "NXattenuator");
	if (experiment_settings.transmission > 0.0) saveDouble(grp,"attenuator_transmission", experiment_settings.transmission,"");
	H5Gclose(grp);

        grp = createGroup(hdf5_file, "/entry/instrument/" DETECTOR_NAME,"NXdetector_group");

	char *group_names = (char *) malloc(24 *sizeof(char *));
	strcpy(group_names, DETECTOR_NAME);
	strcpy(group_names+16,"detector");

	int32_t group_index[2] = {1,2};
	int32_t group_parent[2] = {-1, 1};
	int32_t group_type[2] = {1, 2};

	saveString1D(grp,"group_names", group_names, "", 2, 16);
	saveInt1D(grp, "group_parent", group_parent, "", 2);
	saveInt1D(grp, "group_index", group_index, "", 2);
	saveInt1D(grp, "group_type", group_type, "", 2);
	H5Gclose(grp);
	free(group_names);

        grp = createGroup(hdf5_file, "/entry/instrument/beam","NXbeam");
	saveDouble(grp, "incident_wavelength",12.4/experiment_settings.energy_in_keV,"angstrom");
        saveDouble(grp, "total_flux", experiment_settings.total_flux,"Hz");
        if (experiment_settings.beam_size_x > 0) {
            double beam_size[2] = {experiment_settings.beam_size_x, experiment_settings.beam_size_y};
            saveDouble1D(grp, "incident_beam_size", beam_size, "um", 2);
        }
	H5Gclose(grp);

        grp = createGroup(hdf5_file, "/entry/sample","NXsample");
	H5Gclose(grp);

	grp = createGroup(hdf5_file, "/entry/sample/beam","NXbeam");
	saveDouble(grp, "incident_wavelength",12.4/experiment_settings.energy_in_keV,"angstrom");
	H5Gclose(grp);

	grp = createGroup(hdf5_file, "/entry/sample/goniometer","NXtransformations");
        H5Gclose(grp);

        hid_t det_grp = createGroup(hdf5_file, "/entry/instrument/detector", "NXdetector");
	saveDouble(det_grp,"beam_center_x",experiment_settings.beam_x, "pixel");
	saveDouble(det_grp,"beam_center_y",experiment_settings.beam_y, "pixel");
	saveDouble(det_grp,"count_time",experiment_settings.count_time, "s");
	saveDouble(det_grp,"frame_time",experiment_settings.frame_time, "s");
	saveDouble(det_grp,"detector_distance", experiment_settings.detector_distance / 1000.0, "m");
	saveDouble(det_grp,"sensor_thickness", SENSOR_THICKNESS_IN_UM/1000000.0, "m");
	saveDouble(det_grp,"x_pixel_size", PIXEL_SIZE_IN_UM/1000000.0, "m");
	saveDouble(det_grp,"y_pixel_size", PIXEL_SIZE_IN_UM/1000000.0, "m");
	saveString(det_grp,"sensor_material","Si");
	saveString(det_grp,"description","PSI Jungfrau");
        saveInt(det_grp, "bit_depth_image", experiment_settings.pixel_depth * 8);
        saveInt(det_grp, "bit_depth_readout", 16);
        transform_and_write_mask(det_grp);        
        if (experiment_settings.pixel_depth == 2) {
            saveInt(det_grp,"saturation_value", INT16_MAX-10);
	    saveInt(det_grp,"underload_value", INT16_MIN+10);
        } else {
            saveInt(det_grp,"saturation_value", INT32_MAX-1);
	    saveInt(det_grp,"underload_value", INT32_MIN+1);
        }

        grp = createGroup(hdf5_file, "/entry/instrument/detector/detectorSpecific","NXcollection");
	saveDouble(grp,"photon_energy",experiment_settings.energy_in_keV * 1000.0,"eV");
	saveInt(grp, "nimages",    experiment_settings.nframes_to_write_per_trigger);
	saveInt(grp, "ntrigger",   experiment_settings.ntrigger);
        saveInt(grp, "internal_summation", experiment_settings.summation);
        saveDouble(grp, "internal_frame_time", experiment_settings.frame_time_detector, "s");
        saveDouble(grp, "internal_count_time", experiment_settings.count_time_detector, "s");
        saveInt(grp, "nimages_per_data_file" , writer_settings.images_per_file);
	saveInt(grp, "x_pixels_in_detector", XPIXEL);
	saveInt(grp, "y_pixels_in_detector", YPIXEL);

        if (writer_settings.compression == JF_COMPRESSION_BSHUF_LZ4) saveString(grp,"compression","bslz4");
        else if (writer_settings.compression == JF_COMPRESSION_BSHUF_ZSTD) saveString(grp,"compression","bszstd");
        else if (writer_settings.compression == JF_COMPRESSION_NONE) saveString(grp,"compression","");

        saveTimeUTC(grp, "pedestal_G0_time", time_pedestalG0);
        saveTimeUTC(grp, "pedestal_G1_time", time_pedestalG1);
        saveTimeUTC(grp, "pedestal_G2_time", time_pedestalG2);

        herr_t status = H5Lcreate_hard(det_grp, "pixel_mask", grp, "pixel_mask", H5P_DEFAULT, H5P_DEFAULT);
        H5Gclose(grp);
	H5Gclose(det_grp);

	grp = createGroup(hdf5_file, "/entry/instrument/detector/detectorSpecific/adu_to_photon","");
	saveUInt16_3D(grp, "G0", gain_pedestal.gainG0, 1024, 512, NMODULES * NCARDS, 1.0/(16384.0*512.0));
	saveUInt16_3D(grp, "G1", gain_pedestal.gainG1, 1024, 512, NMODULES * NCARDS, -1.0/8192);
	saveUInt16_3D(grp, "G2", gain_pedestal.gainG2, 1024, 512, NMODULES * NCARDS, -1.0/8192);
	H5Gclose(grp);

	grp = createGroup(hdf5_file, "/entry/instrument/detector/detectorSpecific/pedestal_in_adu","");
	saveUInt16_3D(grp, "G0", gain_pedestal.pedeG0, 1024, 512, NMODULES * NCARDS, 0.25);
	saveUInt16_3D(grp, "G1", gain_pedestal.pedeG1, 1024, 512, NMODULES * NCARDS, 0.25);
	saveUInt16_3D(grp, "G2", gain_pedestal.pedeG2, 1024, 512, NMODULES * NCARDS, 0.25);
	H5Gclose(grp);

        grp = createGroup(hdf5_file, "/entry/instrument/detector/geometry","NXgeometry");
	H5Gclose(grp);

        write_metrology(hdf5_file);

        H5Fclose(hdf5_file);
	return 0;
}

int open_data_hdf5() {
    int data_hdf5_files = experiment_settings.nframes_to_write / writer_settings.images_per_file;
    if (experiment_settings.nframes_to_write % writer_settings.images_per_file > 0) data_hdf5_files++;

    data_hdf5           = (hid_t *) calloc(data_hdf5_files, sizeof(hid_t));
    data_hdf5_group     = (hid_t *) calloc(data_hdf5_files, sizeof(hid_t));
    data_hdf5_dataset   = (hid_t *) calloc(data_hdf5_files, sizeof(hid_t));
    data_hdf5_dcpl      = (hid_t *) calloc(data_hdf5_files, sizeof(hid_t));
    data_hdf5_dataspace = (hid_t *) calloc(data_hdf5_files, sizeof(hid_t));

    for (int i = 0; i < data_hdf5_files; i++) {

        // Calculate number of frames
	int32_t frames = experiment_settings.nframes_to_write - writer_settings.images_per_file * i;
	if (frames >  writer_settings.images_per_file) frames =  writer_settings.images_per_file;
	if (frames <= 0) return 1;

	// generate filename for data file
	char buff[12];
	snprintf(buff,12,"data_%06d", i+1);

        std::string filename = "";
        if (writer_settings.main_location != "") filename =
                        writer_settings.main_location + "/" +
                        writer_settings.HDF5_prefix + "_" + std::string(buff)+".h5";
        else filename = writer_settings.HDF5_prefix + "_" + std::string(buff)+".h5";

	// Create data file
	data_hdf5[i] = H5Fcreate(filename.c_str(), H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
	if (data_hdf5[i] < 0) {
		std::cerr << "Cannot create file: " << filename << std::endl;
		return 1;
	}

	hid_t grp = createGroup(data_hdf5[i], "/entry", "NXentry");
	H5Gclose(grp);

	data_hdf5_group[i] = createGroup(data_hdf5[i], "/entry/data","NXdata");

	herr_t h5ret;

	// https://support.hdfgroup.org/ftp/HDF5/current/src/unpacked/examples/h5_crtdat.c
	hsize_t dims[3], chunk[3];

	dims[0] = frames;
	dims[1] = 514 * NMODULES / 2 * NCARDS;
	dims[2] = 1030*2;

	chunk[0] = 1;
        if (writer_settings.write_hdf5 == true)
	    chunk[1] = 514 * NMODULES / 2;
        else
	    chunk[1] = 514 * NMODULES / 2 * NCARDS;
	chunk[2] = 1030*2;

	// Create the data space for the dataset.
	data_hdf5_dataspace[i] = H5Screate_simple(3, dims, NULL);

	data_hdf5_dcpl[i] = H5Pcreate (H5P_DATASET_CREATE);
	h5ret = H5Pset_chunk (data_hdf5_dcpl[i], 3, chunk);

	// Set appropriate compression filter
        switch (writer_settings.compression) {
            case JF_COMPRESSION_BSHUF_LZ4:
	    {
		unsigned int params[] = {LZ4_BLOCK_SIZE, BSHUF_H5_COMPRESS_LZ4};                
		h5ret = H5Pset_filter(data_hdf5_dcpl[i], (H5Z_filter_t)BSHUF_H5FILTER, H5Z_FLAG_MANDATORY, (size_t)2, params);
		HDF5_ERROR(h5ret,H5Pset_filter);
		break;
	    }
	    case JF_COMPRESSION_BSHUF_ZSTD:
	    {
		unsigned int params[] = {ZSTD_BLOCK_SIZE, BSHUF_H5_COMPRESS_ZSTD};
		h5ret = H5Pset_filter(data_hdf5_dcpl[i], (H5Z_filter_t)BSHUF_H5FILTER, H5Z_FLAG_MANDATORY, (size_t)2, params);
		HDF5_ERROR(h5ret,H5Pset_filter);
		break;
	    }
        }
	// Create the dataset.
        if (experiment_settings.pixel_depth == 2)
	    data_hdf5_dataset[i] = H5Dcreate2(data_hdf5_group[i], "data", H5T_STD_I16LE, data_hdf5_dataspace[i],
			H5P_DEFAULT, data_hdf5_dcpl[i], H5P_DEFAULT);
        else
	    data_hdf5_dataset[i] = H5Dcreate2(data_hdf5_group[i], "data", H5T_STD_I32LE, data_hdf5_dataspace[i],
			H5P_DEFAULT, data_hdf5_dcpl[i], H5P_DEFAULT);

	// Add attributes
	int tmp = i *  writer_settings.images_per_file + 1;
	hid_t aid = H5Screate(H5S_SCALAR);
	hid_t attr = H5Acreate2(data_hdf5_dataset[i], "image_nr_low", H5T_STD_I32LE, aid, H5P_DEFAULT, H5P_DEFAULT);
	h5ret = H5Awrite(attr, H5T_NATIVE_INT, &tmp);
	h5ret = H5Sclose(aid);
	h5ret = H5Aclose(attr);

	tmp = tmp+frames-1;
	aid = H5Screate(H5S_SCALAR);
	attr = H5Acreate2(data_hdf5_dataset[i], "image_nr_high", H5T_STD_I32LE, aid, H5P_DEFAULT, H5P_DEFAULT);
	h5ret = H5Awrite(attr, H5T_NATIVE_INT, &tmp);
	h5ret = H5Sclose(aid);
	h5ret = H5Aclose(attr);
    }
    return 0;
}

int close_data_hdf5() {
    int data_hdf5_files = experiment_settings.nframes_to_write / writer_settings.images_per_file;
    if (experiment_settings.nframes_to_write % writer_settings.images_per_file > 0) data_hdf5_files++;

    for (int i = 0; i < data_hdf5_files; i++) {
	H5Pclose (data_hdf5_dcpl[i]);

	// End access to the dataset and release resources used by it. 
	H5Dclose(data_hdf5_dataset[i]);

	// Terminate access to the data space.
	H5Sclose(data_hdf5_dataspace[i]);

	H5Gclose(data_hdf5_group[i]);
	H5Fclose(data_hdf5[i]);
   }
   free(data_hdf5);
   free(data_hdf5_group);
   free(data_hdf5_dataset);
   free(data_hdf5_dcpl);
   free(data_hdf5_dataspace);
   return 0;
}

int save_data_hdf(char *data, size_t size, size_t frame, int chunk) {
    int file = frame / writer_settings.images_per_file;
    pthread_mutex_lock(&hdf5_mutex);
    // TODO: Ugly workaround - need to figure out, why chunks are wrong
    size_t chunk0;
    if (chunk == 1) chunk0 = 0;
    else chunk0 = 1;
    hsize_t offset[] = {frame % writer_settings.images_per_file, chunk0 * 514 * NMODULES / 2, 0};
    herr_t h5ret = H5Dwrite_chunk(data_hdf5_dataset[file], H5P_DEFAULT, 0, offset, size, data);
    HDF5_ERROR(h5ret,H5Dwrite_chunk);

    pthread_mutex_unlock(&hdf5_mutex);
    return 0;
}

// Pack data
int pack_data_hdf5() {
    char *data = (char *) malloc(XPIXEL*YPIXEL*experiment_settings.pixel_depth* experiment_settings.nframes_to_write);

    for (int i = 0; i < experiment_settings.nframes_to_write; i++) {
        for (int j = 0; j < 2; j++) {
            char buff[12];
            snprintf(buff,12,"%08d_%01d", i, 1-j);
            std::string prefix = "";
            if (writer_settings.nlocations > 0)
                prefix = writer_settings.data_location[i % writer_settings.nlocations] + "/";
            std::string filename = prefix + writer_settings.HDF5_prefix+"_"+std::string(buff) + ".img";

            std::ifstream out_file(filename.c_str(), std::ios::binary | std::ios::in);
             
            if (!out_file.is_open()) {
                std::cerr << filename.c_str() << std::endl;
                return 1;
            }

            out_file.read(data + (2*i + j) * (YPIXEL * XPIXEL)/2 * experiment_settings.pixel_depth, 
                          (YPIXEL*XPIXEL)/2*experiment_settings.pixel_depth);
            out_file.close();
        }
    }
    if (experiment_settings.pixel_depth == 2) {
        int16_t *data_in_16 = (int16_t *) data;
        for (size_t i = 0; i < XPIXEL*YPIXEL* experiment_settings.nframes_to_write; i++) {
            if ((data_in_16[i] < 0 ) && (data_in_16[i] > -30000)) data_in_16[i] = 0;
        }

    } else {
        int32_t *data_in_32 = (int32_t *) data;
        for (size_t i = 0; i < XPIXEL*YPIXEL* experiment_settings.nframes_to_write; i++) {
            if ((data_in_32[i] < 0 ) && (data_in_32[i] > -3000000)) data_in_32[i] = 0;
        }
    }

    int data_hdf5_files = experiment_settings.nframes_to_write / writer_settings.images_per_file;
    if (experiment_settings.nframes_to_write % writer_settings.images_per_file > 0) data_hdf5_files++;

    for (int i = 0 ; i < data_hdf5_files; i++) {
        herr_t h5ret;
        if (experiment_settings.pixel_depth == 2)
            h5ret = H5Dwrite(data_hdf5_dataset[i], H5T_STD_I16LE, H5S_ALL, H5S_ALL, H5P_DEFAULT, data + i*writer_settings.images_per_file*(YPIXEL*XPIXEL *sizeof(int16_t)));
        else
            h5ret = H5Dwrite(data_hdf5_dataset[i], H5T_STD_I32LE, H5S_ALL, H5S_ALL, H5P_DEFAULT, data + i*writer_settings.images_per_file*(YPIXEL*XPIXEL *sizeof(int32_t)));
        HDF5_ERROR(h5ret,H5Dwrite);
    }
    return 0;
}

// Save image data "as is" binary
int write_frame(char *data, size_t size, int frame_id, int thread_id) {
        char buff[12];
        snprintf(buff,12,"%08d_%01d", frame_id, thread_id);
        std::string prefix = "";
        if (writer_settings.nlocations > 0)
                prefix = writer_settings.data_location[frame_id % writer_settings.nlocations] + "/";
        std::string filename = prefix + writer_settings.HDF5_prefix+"_"+std::string(buff) + ".img";
        std::ofstream out_file(filename.c_str(), std::ios::binary | std::ios::out);
        if (!out_file.is_open()) return 1;
        out_file.write(data,size);
        out_file.close();
        return 0;
}
