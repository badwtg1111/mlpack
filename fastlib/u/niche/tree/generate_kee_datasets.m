function [] = generate_kee_datasets(N, mu, sigma, ...
				    min_max_vals, resolution, ...
				    data_filename, ...
				    data_linspace_filename);
%  generate_kee_datasets() - generate N Gaussian data points and save to file
%  Usage:
%  generate_kee_datasets(N, mu, sigma, [min_val max_val], resolution, data_filename, data_linspace_filename);


min_val = min_max_vals(1);
max_val = min_max_vals(2);

% generate Gaussian data
data = normrnd(mu * ones(1,N),sigma);


% linear spacing


linear_spacing = ...
    linspace(min_val, max_val, ...
	     round((max_val - min_val) / resolution));


csvwrite(data_filename, data');

csvwrite(data_linspace_filename, [data linear_spacing]');

