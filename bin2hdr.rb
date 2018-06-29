#!/usr/bin/env ruby

def to_parts(file)
	file.split('.')[0].split(/_|-/)
end

def get_prefix(file)
	parts = to_parts(file)
	prefix = "BCM2_"
	prefix += (parts[0][0] == "r" ? "R" : "W")
	prefix += "CODE_#{parts[1].upcase}_"
end

def emit(name, val, pad = 2)
	printf("#define %s\t\t0x%0*x\n", name, pad, val)
end

def mk_defines(obj)
	symbols = Hash.new

	`nm #{obj}`.each_line do |line|
		sym = line.split(' ')
		name = sym[2]
		next if name[0] == "." || sym[1] != "t"

		symbols[sym[0].to_i(16)] = name
		#symbols[name] = line[0].to_i(16)]
	end

	prefix = get_prefix(obj)

	symbols.sort.each do |val, name|
		next if name[0, 4] != "arg_"
		emit(prefix + "A_#{name[4..-1].upcase}", val)
	end

	puts

	emit(prefix + "ENTRY", symbols.key("main"), 3)
end

def mk_code(bin)
	parts = to_parts(bin)
	data = File.read(bin, mode: "rb")

	emit(get_prefix(bin) + "SIZE", data.size, 3)
	puts

	printf("uint32_t %ccode_%s[] = {", parts[0][0], parts[1])

	i = 0
	while i < data.size
		printf("\n\t") if (i % 16) == 0
		printf("0x%08x, ", data[i, 4].unpack("L>")[0])
		i += 4
	end

	printf("\n};\n")
end

if ARGV[0] == "defines"
	mk_defines(ARGV[1])
elsif ARGV[0] == "code"
	mk_code(ARGV[1])
end
