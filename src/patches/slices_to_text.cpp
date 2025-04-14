#include "lsqecc/patches/slices_to_text.hpp"
#include <sstream>

namespace lsqecc {

// Helper: determine the label for a given cell.
// Here we assume:
// - If the cell is empty, label "route".
// - If the cell has a patch:
//      * For Qubit: if activity is Measurement, label "measurement"; else "qubit".
//      * For Ancilla or Routing: label "ancilla".
//      * For Distillation: label "distillation".
//      * For other types: label "unknown".
std::string get_cell_label(const std::optional<DensePatch>& patch)
{
    if (!patch.has_value()) {
        return "";
    }
    // Determine the label based on patch type and activity.
    switch (patch->type) {
        case PatchType::Distillation:
            return "latticesurgery:distillation";
        case PatchType::PreparedState:
            return "latticesurgery:distillation";
        case PatchType::Qubit:
            return "latticesurgery:qubit";
        case PatchType::Routing:
            return "latticesurgery:routing_1";
        case PatchType::Dead:
            return "latticesurgery:ancilla";
    // Other cases here.
    default:
        return "unknown";
    }
}

// Returns a 6-digit code string representing the state of the cell's boundaries.
// Order: top, bottom, left, right, up, down.
// For top, bottom, left, and right: if the boundary is active and its type is not None, return '1', else '0'.
// For up and down: if the patch type is Qubit, returns "11"; otherwise returns "00".
std::string get_edges_code(const std::optional<DensePatch>& patch)
{
    // No patch? Return all zeros.
    if (!patch.has_value())
    {
        return "";
    }
    const DensePatch& dpatch = *patch;
    auto b_active = [&](const Boundary &b) -> int {
        // If boundary is active and not of type None, we consider it “qualified.”
        return (b.is_active && b.boundary_type != BoundaryType::None) ? 1 : 0;
    };
    int top_digit = b_active(dpatch.boundaries.top);
    int bottom_digit = b_active(dpatch.boundaries.bottom);
    int left_digit = b_active(dpatch.boundaries.left);
    int right_digit = b_active(dpatch.boundaries.right);
    std::string up_down;
    // For the "up" and "down" components:
    if (dpatch.type == PatchType::Qubit)
        up_down = "11";
    else
        up_down = "00";
    return std::to_string(top_digit)
         + std::to_string(bottom_digit)
         + std::to_string(left_digit)
         + std::to_string(right_digit)
         + up_down;
}

// Converts a single DenseSlice to text.
std::string slice_to_text(const DenseSlice& slice, size_t time_stamp)
{
    std::ostringstream oss;
    // Get grid dimensions from the layout.
    Cell furthest = slice.get_layout().furthest_cell();
    for (int row = 0; row <= furthest.row; ++row)
    {
        for (int col = 0; col <= furthest.col; ++col)
        {
            // Get the cell coordinates.
            Cell cell = Cell::from_ints(row, col);
            // Get the patch (if any) from the slice.
            const auto& patch = slice.patch_at(cell);
            // Determine the basic label.
            std::string label = get_cell_label(patch);
            // Append the six-digit code representing edge info.
            std::string edges = get_edges_code(patch);
            // Print the coordinate in the format (row,col,time) followed by " label_edges".
            if (label.empty())
            {
                // If the label is empty, we skip this cell.
                continue;
            }
            oss << "(" << row << "," << col << "," << time_stamp << ") " 
                << label << "_" << edges << "\n";
        }
    }
    return oss.str();
}

// Converts a vector of DenseSlices (representing time slices) to text.
std::string slices_to_text(const std::vector<DenseSlice>& slices)
{
    std::ostringstream oss;
    for (size_t t = 0; t < slices.size(); ++t) {
        oss << slice_to_text(slices[t], t) << "\n";
    }
    return oss.str();
}

} // namespace lsqecc