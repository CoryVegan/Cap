#define BOOST_TEST_MODULE ExactTransientSolution
#define BOOST_TEST_MAIN
#include <cap/energy_storage_device.h>
#include <cap/mp_values.h>
#include <deal.II/base/types.h>
#include <deal.II/grid/grid_generator.h>
#include <deal.II/dofs/dof_handler.h>
#include <boost/test/unit_test.hpp>
#include <boost/foreach.hpp>
#include <boost/format.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <cmath>
#include <iostream>
#include <fstream>
#include <numeric>

namespace cap {

void compute_parameters(std::shared_ptr<boost::property_tree::ptree const> input_database,
                        std::shared_ptr<boost::property_tree::ptree      > output_database)
{
    double const sandwich_height = input_database->get<double>("geometry.sandwich_height");
    double const cross_sectional_area = sandwich_height * 1.0;
    double const electrode_width = input_database->get<double>("geometry.anode_electrode_width");
    double const separator_width = input_database->get<double>("geometry.separator_width");

    // getting the material parameters values
    std::shared_ptr<boost::property_tree::ptree> material_properties_database = 
        std::make_shared<boost::property_tree::ptree>(input_database->get_child("material_properties"));
    cap::MPValuesParameters<2> mp_values_params(material_properties_database);
    std::shared_ptr<boost::property_tree::ptree> geometry_database = 
        std::make_shared<boost::property_tree::ptree>(input_database->get_child("geometry"));
    mp_values_params.geometry = std::make_shared<cap::DummyGeometry<2>>(geometry_database);
    std::shared_ptr<cap::MPValues<2> > mp_values = std::shared_ptr<cap::MPValues<2> >
        (new cap::MPValues<2>(mp_values_params));
    // build dummy cell itertor and set its material id
    dealii::Triangulation<2> triangulation;
    dealii::GridGenerator::hyper_cube (triangulation);
    dealii::DoFHandler<2> dof_handler(triangulation);
    dealii::DoFHandler<2>::active_cell_iterator cell = 
        dof_handler.begin_active();
    // electrode
    cell->set_material_id(
        input_database->get<dealii::types::material_id>("material_properties.anode_electrode_material_id"));
    std::vector<double> electrode_solid_electrical_conductivity_values(1);
    std::vector<double> electrode_liquid_electrical_conductivity_values(1);
    std::vector<double> electrode_specific_capacitance_values(1);
    std::vector<double> electrode_exchange_current_density_values(1);
    std::vector<double> electrode_electron_thermal_voltage_values(1);
    mp_values->get_values("solid_electrical_conductivity" , cell, electrode_solid_electrical_conductivity_values );
    mp_values->get_values("liquid_electrical_conductivity", cell, electrode_liquid_electrical_conductivity_values);
    mp_values->get_values("specific_capacitance"          , cell, electrode_specific_capacitance_values          );
    mp_values->get_values("faradaic_reaction_coefficient" , cell, electrode_exchange_current_density_values      );
    double const cell_current_density = 1.0;
    double const initial_voltage      = 1.0;
    double const dimensionless_cell_current_density =
        cell_current_density * electrode_width *
        (electrode_liquid_electrical_conductivity_values[0] + electrode_solid_electrical_conductivity_values[0])
        / (electrode_liquid_electrical_conductivity_values[0] * electrode_solid_electrical_conductivity_values[0] * initial_voltage);
    double const ratio_of_solution_phase_to_matrix_phase_conductivities =
        electrode_liquid_electrical_conductivity_values[0] / electrode_solid_electrical_conductivity_values[0];
    if (electrode_exchange_current_density_values[0] != 0.0)
        throw std::runtime_error("test assumes no faradaic processes, exchange_current_density has to be zero");
        
    output_database->put("dimensionless_cell_current_density"                    , dimensionless_cell_current_density                    );
    output_database->put("ratio_of_solution_phase_to_matrix_phase_conductivities", ratio_of_solution_phase_to_matrix_phase_conductivities);

    output_database->put("position_normalization_factor", electrode_width);
    output_database->put("time_normalization_factor"    ,
        electrode_specific_capacitance_values[0] 
            * ( 1.0 / electrode_solid_electrical_conductivity_values[0]
              + 1.0 / electrode_liquid_electrical_conductivity_values[0] )
            * std::pow(electrode_width,2) );
    double const electrode_resistance = electrode_width 
        * (electrode_solid_electrical_conductivity_values[0] + electrode_liquid_electrical_conductivity_values[0])
        / (electrode_solid_electrical_conductivity_values[0] * electrode_liquid_electrical_conductivity_values[0]);
          
    // separator
    cell->set_material_id(
        input_database->get<dealii::types::material_id>("material_properties.separator_material_id"));
    std::vector<double> separator_liquid_electrical_conductivity_values(1);
    mp_values->get_values("liquid_electrical_conductivity", cell, separator_liquid_electrical_conductivity_values);
    double const separator_resitance = separator_width / separator_liquid_electrical_conductivity_values[0];

    double const ratio_of_separator_to_electrode_resistances = separator_resitance / electrode_resistance;
    output_database->put("ratio_of_separator_to_electrode_resistances", ratio_of_separator_to_electrode_resistances);
    output_database->put("cross_sectional_area"                       , cross_sectional_area                       );
}



void verification_problem(std::shared_ptr<cap::EnergyStorageDevice> dev, std::shared_ptr<boost::property_tree::ptree const> database, std::ostream & os = std::cout)
{
    double dimensionless_cell_current_density                     = database->get<double>("dimensionless_cell_current_density"                    );
    double ratio_of_solution_phase_to_matrix_phase_conductivities = database->get<double>("ratio_of_solution_phase_to_matrix_phase_conductivities");
    double ratio_of_separator_to_electrode_resistances            = database->get<double>("ratio_of_separator_to_electrode_resistances"           );

    int    const infty = database->get<int>("terms_in_truncation_of_infinite_series");
    double const pi    = std::acos(-1.0);

    auto compute_dimensionless_overpotential =
        [infty, pi,
        &ratio_of_solution_phase_to_matrix_phase_conductivities, &dimensionless_cell_current_density]
        (double const dimensionless_time, double const dimensionless_position)
        {
            std::vector<double> coefficients(infty);
            for (int n = 0; n < infty; ++n) {
                coefficients[n] =
                ((n % 2 == 0 ? 1.0 : -1.0) + ratio_of_solution_phase_to_matrix_phase_conductivities)
                / std::pow(n,2)
                * std::cos( n * pi * dimensionless_position )
                * std::exp( - std::pow(n,2) * std::pow(pi,2) * dimensionless_time );
            }
            return 
                dimensionless_cell_current_density * dimensionless_time
                +
                dimensionless_cell_current_density * (3.0 * std::pow(dimensionless_position,2) - 1.0)
                    / (6.0 * (1.0 + ratio_of_solution_phase_to_matrix_phase_conductivities))
                +
                dimensionless_cell_current_density * ratio_of_solution_phase_to_matrix_phase_conductivities
                    * (3.0 * std::pow(dimensionless_position,2) + 2.0 - 6.0 * dimensionless_position)
                    / (6.0 * (1.0 + ratio_of_solution_phase_to_matrix_phase_conductivities))
                -
                2.0 * dimensionless_cell_current_density
                   / (std::pow(pi,2) * (1.0 + ratio_of_solution_phase_to_matrix_phase_conductivities))
                   * std::accumulate(&(coefficients[1]), &(coefficients[infty]), 0.0);
        };

    auto compute_dimensionless_cell_voltage =
        [infty, pi,
        &ratio_of_solution_phase_to_matrix_phase_conductivities, &dimensionless_cell_current_density, &ratio_of_separator_to_electrode_resistances]
        (double const dimensionless_time)
        {
            std::vector<double> coefficients(infty);
            for (int n = 0; n < infty; ++n) {
                coefficients[n] =
                std::pow(
                    ((n % 2 == 0 ? 1.0 : -1.0) * ratio_of_solution_phase_to_matrix_phase_conductivities + 1.0)
                    / (ratio_of_solution_phase_to_matrix_phase_conductivities + 1.0)
                  , 2)
                / (std::pow(n,2) * std::pow(pi,2))
                * std::exp( - std::pow(n,2) * std::pow(pi,2) * dimensionless_time );
            }
            return
                1.0
                - dimensionless_cell_current_density * ( 1.0 / 3.0 + dimensionless_time - 2.0 * std::accumulate(&(coefficients[1]), &(coefficients[infty]), 0.0))
                - 0.5 * ratio_of_separator_to_electrode_resistances * dimensionless_cell_current_density;
        };

    auto compute_dimensionless_double_layer_current_density =
        [infty, pi,
        &ratio_of_solution_phase_to_matrix_phase_conductivities, &dimensionless_cell_current_density]
        (double const dimensionless_time, double const dimensionless_position)
        {
            std::vector<double> coefficients(infty);
            for (int n = 0; n < infty; ++n) {
                coefficients[n] =
                ((n % 2 == 0 ? 1.0 : -1.0) + ratio_of_solution_phase_to_matrix_phase_conductivities)
                * std::cos( n * pi * dimensionless_position )
                * std::exp( - std::pow(n,2) * std::pow(pi,2) * dimensionless_time );
            }
            return
                1.0 + 2.0 / (1.0 + ratio_of_solution_phase_to_matrix_phase_conductivities) * std::accumulate(&(coefficients[1]), &(coefficients[infty]), 0.0);
        };

    auto compute_dimensionless_complex_impedance =
        [&ratio_of_solution_phase_to_matrix_phase_conductivities, &ratio_of_separator_to_electrode_resistances]
        (double const dimensionless_angular_frequency)
        {
            double const dimensionless_real_impedance =
                (1.0 + std::pow(ratio_of_solution_phase_to_matrix_phase_conductivities,2))
                    / (std::pow(1.0 + ratio_of_solution_phase_to_matrix_phase_conductivities,2) * dimensionless_angular_frequency)
                    * (std::sinh(dimensionless_angular_frequency) * std::cosh(dimensionless_angular_frequency) - std::sin(dimensionless_angular_frequency) * std::cos(dimensionless_angular_frequency))
                    / (std::pow(std::cosh(dimensionless_angular_frequency),2) - std::pow(std::cos(dimensionless_angular_frequency),2))
                + 2.0 * ratio_of_solution_phase_to_matrix_phase_conductivities
                    / (std::pow(1.0 + ratio_of_solution_phase_to_matrix_phase_conductivities,2) * dimensionless_angular_frequency)
                    * (std::sinh(dimensionless_angular_frequency) * std::cos(dimensionless_angular_frequency) - std::cosh(dimensionless_angular_frequency) * std::sin(dimensionless_angular_frequency))
                    / (std::pow(std::cosh(dimensionless_angular_frequency),2) - std::pow(std::cos(dimensionless_angular_frequency),2))
                + 2.0 * ratio_of_solution_phase_to_matrix_phase_conductivities
                    / std::pow(1.0 + ratio_of_solution_phase_to_matrix_phase_conductivities,2)
                + ratio_of_separator_to_electrode_resistances;
            double const dimensionless_imaginary_impedance =
                (1.0 + std::pow(ratio_of_solution_phase_to_matrix_phase_conductivities,2))
                    / (std::pow(1.0 + ratio_of_solution_phase_to_matrix_phase_conductivities,2) * dimensionless_angular_frequency)
                    * (std::sinh(dimensionless_angular_frequency) * std::cosh(dimensionless_angular_frequency) + std::sin(dimensionless_angular_frequency) * std::cos(dimensionless_angular_frequency))
                    / (std::pow(std::cosh(dimensionless_angular_frequency),2) - std::pow(std::cos(dimensionless_angular_frequency),2))
                + 2.0 * ratio_of_solution_phase_to_matrix_phase_conductivities
                    / (std::pow(1.0 + ratio_of_solution_phase_to_matrix_phase_conductivities,2) * dimensionless_angular_frequency)
                    * (std::sinh(dimensionless_angular_frequency) * std::cos(dimensionless_angular_frequency) + std::cosh(dimensionless_angular_frequency) * std::sin(dimensionless_angular_frequency))
                    / (std::pow(std::cosh(dimensionless_angular_frequency),2) - std::pow(std::cos(dimensionless_angular_frequency),2));
            return std::complex<double>(dimensionless_real_impedance, dimensionless_imaginary_impedance);
        };

    // exact vs computed
    double const discharge_current = database->get<double>("discharge_current");
    double const discharge_time    = database->get<double>("discharge_time"   );
    double const time_step         = database->get<double>("time_step"        );
    double const epsilon = time_step * 1.0e-4;
    double const cross_sectional_area         = database->get<double>("cross_sectional_area"        );
    double const time_normalization_factor    = database->get<double>("time_normalization_factor"   );
//    double const voltage_normalization_factor = database->get<double>("voltage_normalization_factor");
    double const frequency_normalization_factor = 2.0 / time_normalization_factor;
    double const impedance_normalization_factor = dimensionless_cell_current_density;
    double const initial_voltage              = database->get<double>("initial_voltage"             );
    dimensionless_cell_current_density *= discharge_current / cross_sectional_area;
    dimensionless_cell_current_density /= 0.5 * initial_voltage;

    std::cout<<"I^*   = "<<dimensionless_cell_current_density<<"\n";
    std::cout<<"gamma = "<<ratio_of_solution_phase_to_matrix_phase_conductivities<<"\n";
    std::cout<<"beta  = "<<ratio_of_separator_to_electrode_resistances<<"\n";

    dev->reset_voltage(initial_voltage);
    double computed_voltage;
    double exact_voltage;
    for (double time = 0.0; time <= discharge_time+epsilon; time += time_step)
    {
        double const dimensionless_time = (time+time_step) / time_normalization_factor;
        double const dimensionless_cell_voltage = compute_dimensionless_cell_voltage(dimensionless_time);
        exact_voltage = initial_voltage * dimensionless_cell_voltage;
        dev->evolve_one_time_step_constant_current(time_step, -discharge_current);
        dev->get_voltage(computed_voltage);
        os<<boost::format("  %22.15e  %22.15e  %22.15e  \n")
                % time 
                % exact_voltage
                % computed_voltage
                ;
    }
    double const percent_tolerance = database->get<double>("percent_tolerance");
    BOOST_CHECK_CLOSE(computed_voltage, exact_voltage, percent_tolerance);

    // impedance spectroscopy
    std::fstream fout;
    double const frequency_upper_limit = database->get<double>("frequency_upper_limit");
    double const frequency_lower_limit = database->get<double>("frequency_lower_limit");
    int    const steps_per_decade      = database->get<int   >("steps_per_decade"     );
    fout.open("impedance_spectroscopy_data_verification", std::fstream::out);
    fout<<"# impedance Z(f) = R + i X \n";
    fout<<boost::format( "# %22s  %22s  %22s  %22s  %22s  \n")
        % "frequency_f_[Hz]"
        % "resistance_R_[ohm]"
        % "reactance_X_[ohm]"
        % "magnitude_|Z|_[ohm]"
        % "phase_arg(Z)_[degree]"
        ;
    for (double frequency = frequency_upper_limit; frequency >= frequency_lower_limit; frequency /= std::pow(10.0, 1.0/steps_per_decade))
    {
        double const dimensionless_angular_frequency =
            std::sqrt(2.0 * pi * frequency / frequency_normalization_factor);
        std::complex<double> const dimensionless_complex_impedance = compute_dimensionless_complex_impedance(dimensionless_angular_frequency);
        std::complex<double> const impedance = std::conj(impedance_normalization_factor * dimensionless_complex_impedance);
        fout<<boost::format( "  %22.15e  %22.15e  %22.15e  %22.15e  %22.15e  \n")
            % frequency
            % impedance.real()
            % impedance.imag()
            % std::abs(impedance)
            % (std::arg(impedance) * 180.0 / pi)
            ;
    }
    fout.close();

    // figure 2
    dimensionless_cell_current_density                     = 1.0;
    ratio_of_solution_phase_to_matrix_phase_conductivities = 0.0;
    ratio_of_separator_to_electrode_resistances            = 0.0;
    int const n = 100;
    std::vector<double> zeta(n+1);
    for (int i = 0; i < n+1; ++i)
        zeta[i] = static_cast<double>(i) / n;
    std::vector<double> tau(zeta);
    for (double beta : { 0.0, 1.0 })
    {
        ratio_of_separator_to_electrode_resistances = beta;
        for (double I_star : { 1.0, 0.5, 0.01 })
        {
            std::transform(zeta.cbegin(), zeta.cend(), tau.begin(), [I_star](double const x) { return x / I_star; });
            dimensionless_cell_current_density = I_star;
            fout.open("Srinivasan_fig2_"+std::to_string(beta)+"_"+std::to_string(I_star), std::fstream::out);
            for (double const & dimensionless_time : tau)
            {
                double const relative_utilization = dimensionless_time * dimensionless_cell_current_density;
                double const dimensionless_cell_voltage = compute_dimensionless_cell_voltage(dimensionless_time);
                fout<<boost::format("  %22.15e  %22.15e  \n")
                    % relative_utilization
                    % dimensionless_cell_voltage
                    ;
            }
            fout.close();
        }
    }

    // figure 3
    dimensionless_cell_current_density                     = 1.0;
    ratio_of_solution_phase_to_matrix_phase_conductivities = 0.0;
    ratio_of_separator_to_electrode_resistances            = 0.0;

    for (double const & dimensionless_time : { 0.02, 0.04, 0.1, 0.2, 0.4, 0.65 })
    {
        fout.open("Srinivasan_fig3_"+std::to_string(dimensionless_time), std::fstream::out);
        for (double const & dimensionless_position : zeta)
        {
            double const dimensionless_double_layer_current_density = compute_dimensionless_double_layer_current_density(dimensionless_time, dimensionless_position);
            fout<<boost::format("  %22.15e  %22.15e  \n")
                % dimensionless_position
                % dimensionless_double_layer_current_density
                ;
        }
        fout.close();
    }

    // figure 4
    dimensionless_cell_current_density                     = 1.0;
    ratio_of_solution_phase_to_matrix_phase_conductivities = 0.0;
    ratio_of_separator_to_electrode_resistances            = 0.0;
    for (double const & dimensionless_time : { 0.02, 0.04, 0.1, 0.2, 0.4, 0.65 })
    {
        fout.open("Srinivasan_fig4_"+std::to_string(dimensionless_time), std::fstream::out);
        for (double const & dimensionless_position : zeta)
        {
            double const dimensionless_overpotential = compute_dimensionless_overpotential(dimensionless_time, dimensionless_position);
            fout<<boost::format("  %22.15e  %22.15e  \n")
                % dimensionless_position
                % dimensionless_overpotential
                ;
        }
        fout.close();
    }

    // figure 5
    ratio_of_separator_to_electrode_resistances            = 0.0;
    for (double const & gamma : {0.0, 0.1, 1.0, 10.0, 1.0e6})
    {
        ratio_of_solution_phase_to_matrix_phase_conductivities = gamma;
        for (double const & I_star : { 1.0, 2.0 })
        {
            std::transform(zeta.cbegin(), zeta.cend(), tau.begin(), [I_star](double const x) { return x / I_star; });
            dimensionless_cell_current_density = I_star;
            fout.open("Srinivasan_fig5_"+std::to_string(gamma)+"_"+std::to_string(I_star), std::fstream::out);
            for (double const & dimensionless_time : tau)
            {
                double const relative_utilization = dimensionless_time * dimensionless_cell_current_density;
                double const dimensionless_cell_voltage = compute_dimensionless_cell_voltage(dimensionless_time);
                fout<<boost::format("  %22.15e  %22.15e  \n")
                    % relative_utilization
                    % dimensionless_cell_voltage
                    ;
            }
            fout.close();
        }
    }

    // figure 6
    dimensionless_cell_current_density                     = 1.0;
    ratio_of_solution_phase_to_matrix_phase_conductivities = 1.0;
    ratio_of_separator_to_electrode_resistances            = 0.0;
    for (double const & dimensionless_time : { 0.02, 0.04, 0.1, 0.2, 0.4, 0.65 })
    {
        fout.open("Srinivasan_fig6_"+std::to_string(dimensionless_time), std::fstream::out);
        for (double const & dimensionless_position : zeta)
        {
            double const dimensionless_double_layer_current_density = compute_dimensionless_double_layer_current_density(dimensionless_time, dimensionless_position);
            fout<<boost::format("  %22.15e  %22.15e  \n")
                % dimensionless_position
                % dimensionless_double_layer_current_density
                ;
        }
        fout.close();
    }

    // figure 11
    ratio_of_separator_to_electrode_resistances = 0.0;
    int const m = 1000;
    std::vector<double> omega_star(m+1);
    for (int i = 0; i < m+1; ++i)
        omega_star[i] = 1.0 + 99.0 * static_cast<double>(i) / m;
    for (double const & gamma : { 0.0, 0.1, 1.0, 10.0, 1.0e6})
    {
        fout.open("Srinivasan_fig11_"+std::to_string(gamma), std::fstream::out);
        ratio_of_solution_phase_to_matrix_phase_conductivities = gamma;
        for (double const & dimensionless_angular_frequency : omega_star)
        {
            std::complex<double> const dimensionless_complex_impedance = compute_dimensionless_complex_impedance(dimensionless_angular_frequency);
            fout<<boost::format("  %22.15e  %22.15e  %22.15e  \n")
                % dimensionless_angular_frequency
                % dimensionless_complex_impedance.real()
                % dimensionless_complex_impedance.imag()
                ;
        }
        fout.close();
    }

}

} // end namespace cap

BOOST_AUTO_TEST_CASE( test_exact_transient_solution )
{
    // parse input file
    std::shared_ptr<boost::property_tree::ptree> input_database =
        std::make_shared<boost::property_tree::ptree>();
    read_xml("input_verification_problem", *input_database);

    // remove faradaic processes
    input_database->put("device.material_properties.electrode_material.exchange_current_density", 0.0);

    // build an energy storage system
    std::shared_ptr<boost::property_tree::ptree> device_database =
        std::make_shared<boost::property_tree::ptree>(input_database->get_child("device"));
    std::shared_ptr<cap::EnergyStorageDevice> device =
        cap::buildEnergyStorageDevice(std::make_shared<cap::Parameters>(device_database));

    // measure discharge curve
    std::fstream fout;
    fout.open("verification_problem_data", std::fstream::out);

    std::shared_ptr<boost::property_tree::ptree> verification_problem_database =
        std::make_shared<boost::property_tree::ptree>(input_database->get_child("verification_problem_srinivasan"));

    cap::compute_parameters(device_database, verification_problem_database);

    cap::verification_problem(device, verification_problem_database, fout);

    fout.close();
}    
