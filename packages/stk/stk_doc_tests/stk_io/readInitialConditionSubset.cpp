#include <gtest/gtest.h>                // for AssertHelper, ASSERT_EQ, etc
#include <stddef.h>                     // for size_t
#include <unistd.h>                     // for unlink
#include <stk_io/StkMeshIoBroker.hpp>   // for StkMeshIoBroker
#include <stk_mesh/base/Field.hpp>      // for Field
#include <stk_mesh/base/GetEntities.hpp>  // for get_entities
#include <stk_mesh/base/MetaData.hpp>   // for MetaData, put_field
#include <string>                       // for allocator, operator+, etc
#include <vector>                       // for vector
#include "Ioss_DBUsage.h"               // for DatabaseUsage::READ_MODEL
#include "Ioss_IOFactory.h"             // for IOFactory
#include "Ioss_NodeSet.h"               // for NodeSet
#include "Ioss_Region.h"                // for NodeSetContainer, Region
#include "Ioss_Utils.h"                 // for Utils
#include "stk_io/DatabasePurpose.hpp"   // for DatabasePurpose::READ_MESH, etc
#include "stk_mesh/base/Entity.hpp"     // for Entity
#include "stk_mesh/base/FieldBase.hpp"  // for field_data
#include "stk_mesh/base/Part.hpp"       // for Part
#include "stk_mesh/base/Selector.hpp"   // for Selector
#include "stk_mesh/base/Types.hpp"      // for PartVector
#include "stk_topology/topology.hpp"    // for topology, etc
#include "stk_util/parallel/Parallel.hpp"
namespace Ioss { class DatabaseIO; }

namespace {

  TEST(StkMeshIoBrokerHowTo, readInitialConditionSubset)
  {
    //-BEGIN
    std::string dbFieldNameShell = "ElementBlock_1";
    std::string appFieldName = "pressure";
    
    MPI_Comm communicator = MPI_COMM_WORLD;
    int numProcs = stk::parallel_machine_size(communicator);
    if (numProcs != 1) {
      return;
    }

    {
      // ============================================================
      // INITIALIZATION
      //+ Create a generated mesh containg hexes and shells with a
      //+ single element variable -- ElementBlock_1
      std::string input_filename = "9x9x9|shell:xyzXYZ|variables:element,1|times:1";

      stk::io::StkMeshIoBroker stkIo(communicator);
      stkIo.add_mesh_database(input_filename, "generated", stk::io::READ_MESH);
      stkIo.create_input_mesh();

      stk::mesh::MetaData &meta_data = stkIo.meta_data();

      // Declare the element "pressure" field...
      stk::mesh::Field<double> &pressure = stkIo.meta_data().
	declare_field<stk::mesh::Field<double> >(stk::topology::ELEMENT_RANK, appFieldName,1);

      // "ElementBlock_1" is the name of the element field on the input mesh.
      stk::io::MeshField mf(pressure, dbFieldNameShell);

      const stk::mesh::PartVector &all_parts = meta_data.get_mesh_parts();
      for (size_t i=0; i < all_parts.size(); i++) {
	const stk::mesh::Part *part = all_parts[i];
	
	//+ Put the field on all element block parts...
	stk::mesh::put_field(pressure, *part);

	stk::topology topo = part->topology();
	if (topo == stk::topology::SHELL_QUAD_4) {

	  //+ But only initialize the "pressure" field from mesh data on the shell parts.
	  mf.add_subset(*part);
	}
      }

      stkIo.add_input_field(mf);
      stkIo.populate_bulk_data();

      double time = stkIo.get_input_io_region()->get_state_time(1);

      //+ Populate the fields with data from the input mesh.
      stkIo.read_defined_input_fields(time);

      //-END
      // ============================================================
      //+ VERIFICATION
      //+ The value of the field on the first 729 elements should be 0.0;
      //+ The value of the field on the remaining elements should be sqrt(i+1)
      std::vector<stk::mesh::Entity> elements;
      stk::mesh::get_entities(stkIo.bulk_data(), stk::topology::ELEMENT_RANK,
                              elements);
      EXPECT_TRUE(elements.size() >= 729);
      
      for(size_t i=0; i<729; i++) {
        double *fieldDataForElement = stk::mesh::field_data(pressure, elements[i]);
        EXPECT_DOUBLE_EQ(0.0, *fieldDataForElement);
      }

      for(size_t i=729; i<elements.size(); i++) {
        double *fieldDataForElement = stk::mesh::field_data(pressure, elements[i]);
        EXPECT_DOUBLE_EQ(sqrt(i+1), *fieldDataForElement);
      }
    }
  }
}