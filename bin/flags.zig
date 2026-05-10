const std = @import("std");
const Allocator = std.mem.Allocator;
const assert = std.debug.assert;

pub fn ParseResult(comptime T: type) type {
    return union(enum) {
        flags: T,
        err: ParseError(T),
    };
}

pub fn ParseError(comptime T: type) type {
    return union(enum) {
        bad_short: u8,
        duplicate_arg: []const u8,
        enum_variant_not_found: []const u8,
        expected_flags: std.EnumSet(std.meta.FieldEnum(T)),
        expected_integer: []const u8,
        expected_program_name: void,
        expected_value: []const u8,
        invalid_short_position: u8,
        switch_value: []const u8,
        unexpected_end_of_input: void,
        unexpected_positional: []const u8,
        unknown_flag: []const u8,
    };
}

pub fn parseFromIterator(
    comptime T: type,
    it: anytype,
    allocator: Allocator,
) Allocator.Error!ParseResult(T) {
    @setEvalBranchQuota(5000);

    var args = it;

    const info = comptime getInfo(T);

    var result: T = undefined;
    var uninit: std.EnumSet(std.meta.FieldEnum(T)) = .initFull();

    var positional_index: usize = 0;
    var rest_args: std.ArrayListUnmanaged([]const u8) = .empty;
    defer if (info.variadic == null) assert(rest_args.items.len == 0);
    parse_loop: while (args.next()) |arg| {
        if (arg.len == 0) {
            continue;
        }
        if (arg[0] == '-') {
            const rest = arg[1..];
            if (rest.len == 0) {
                return .{ .err = .unexpected_end_of_input };
            }
            // This tells us that we have a long (`--XXXX`) flag
            if (rest[0] == '-') {
                const eq_idx = std.mem.indexOfScalar(u8, rest, '=');
                // Gets the flag name and, if present, the value after `=`
                const kv: struct { []const u8, ?[]const u8 } =
                    if (eq_idx) |i|
                        .{ rest[1..i], rest[i + 1 ..] }
                    else
                        .{ rest[1..], null };
                var found = false;
                inline for (info.flags) |flag| {
                    if (std.ascii.eqlIgnoreCase(flag.name, kv[0])) {
                        const f = @field(std.meta.FieldEnum(T), flag.name);
                        if (!uninit.contains(f)) {
                            return .{ .err = .{ .duplicate_arg = "--" ++ flag.name } };
                        }
                        found = true;
                        uninit.remove(f);
                        const err = setFlagValue(
                            T,
                            @TypeOf(args),
                            &result,
                            flag,
                            arg[0 .. eq_idx orelse arg.len],
                            kv[1],
                            &args,
                        );
                        if (err) |e| {
                            return .{ .err = e };
                        }
                        if (flag.standalone) {
                            return .{ .flags = result };
                        }
                    }
                }
                if (!found) {
                    return .{ .err = .{ .unknown_flag = arg[0 .. eq_idx orelse arg.len] } };
                }
                continue :parse_loop;
            }
            const shorts_end = std.mem.indexOfScalar(u8, rest, '=') orelse rest.len;
            // Parse all but the last short (i.e. in `-abc`, parses `a` and `b`)
            for (rest[0 .. shorts_end - 1]) |sw| {
                var found = false;
                inline for (info.flags) |flag| {
                    if (flag.short != null and flag.short.? == sw) {
                        const f = @field(std.meta.FieldEnum(T), flag.name);
                        if (!uninit.contains(f)) {
                            return .{ .err = .{ .duplicate_arg = &.{ '-', sw } } };
                        }
                        found = true;
                        // All of the shorts in this position must be switches
                        if (flag.type != .boolean) {
                            return .{ .err = .{ .invalid_short_position = sw } };
                        }
                        uninit.remove(f);
                        @field(result, flag.name) = true;
                        if (flag.standalone) {
                            return .{ .flags = result };
                        }
                    }
                }
                if (!found) {
                    return .{ .err = .{ .unknown_flag = &.{ '-', sw } } };
                }
            }
            // Parse the last short. This short can take a value
            const short = rest[shorts_end - 1];
            const short_value = if (shorts_end != rest.len)
                rest[shorts_end + 1 ..]
            else
                null;
            var found = false;
            inline for (info.flags) |flag| {
                if (flag.short != null and flag.short.? == short) {
                    const f = @field(std.meta.FieldEnum(T), flag.name);
                    if (!uninit.contains(f)) {
                        return .{ .err = .{ .duplicate_arg = &.{ '-', short } } };
                    }
                    found = true;
                    uninit.remove(f);
                    const err = setFlagValue(
                        T,
                        @TypeOf(args),
                        &result,
                        flag,
                        &.{ '-', short },
                        short_value,
                        &args,
                    );
                    if (err) |e| {
                        return .{ .err = e };
                    }
                    if (flag.standalone) {
                        return .{ .flags = result };
                    }
                }
            }
            if (!found) {
                return .{ .err = .{ .unknown_flag = &.{ '-', short } } };
            }
            continue :parse_loop;
        }

        // In this branch, we have a positional argument
        if (positional_index >= info.positionals.len) {
            if (info.variadic != null) {
                try rest_args.append(allocator, arg);
            } else {
                return .{ .err = .{ .unexpected_positional = arg } };
            }
        }
        inline for (info.positionals, 0..) |pos, i| {
            if (i == positional_index) {
                const f = @field(std.meta.FieldEnum(T), pos.name);
                assert(uninit.contains(f));
                uninit.remove(f);
                @field(result, pos.name) = arg;
            }
        }
        positional_index += 1;
    }

    // Initialize default flags
    inline for (info.flags) |flag| {
        if (uninit.contains(@field(std.meta.FieldEnum(T), flag.name)) and flag.isOptional()) {
            @field(result, flag.name) = if (flag.default_value) |dv|
                @as(*const flag.raw_type, @ptrCast(@alignCast(dv))).*
            else switch (@typeInfo(flag.raw_type)) {
                .bool => false,
                .optional => null,
                else => @panic("should be unreachable"),
            };
            uninit.remove(@field(std.meta.FieldEnum(T), flag.name));
        }
    }
    // Initialize default positional arguments
    inline for (info.positionals) |pos| {
        if (uninit.contains(@field(std.meta.FieldEnum(T), pos.name)) and pos.isOptional()) {
            @field(result, pos.name) = if (pos.default_value) |dv|
                @as(*const pos.raw_type, @ptrCast(@alignCast(dv))).*
            else switch (@typeInfo(pos.raw_type)) {
                .optional => null,
                else => @panic("should be unreachable"),
            };
            uninit.remove(@field(std.meta.FieldEnum(T), pos.name));
        }
    }

    // Finalize variadic arguments
    if (info.variadic) |variadic| {
        @field(result, variadic.name) = try rest_args.toOwnedSlice(allocator);
        uninit.remove(@field(std.meta.FieldEnum(T), variadic.name));
    }

    // Check if we didn't encounter flags that we wanted
    if (uninit.count() > 0) {
        return .{ .err = .{ .expected_flags = uninit } };
    }

    return .{ .flags = result };
}

pub fn printErrorDefault(
    comptime T: type,
    writer: *std.Io.Writer,
    err: ParseError(T),
) std.Io.Writer.Error!void {
    return switch (err) {
        .bad_short => |short| writer.print("unrecognized flag '-{s}'", .{&.{short}}),
        .duplicate_arg => |arg| writer.print("duplicate flag '{s}'", .{arg}),
        .enum_variant_not_found => |variant| writer.print("unrecognized variant '{s}'", .{variant}),
        .expected_flags => |flags| {
            var it = flags.iterator();
            const first = it.next() orelse @panic("should not have empty EnumSet");
            if (flags.count() == 1) {
                try writer.print("'--{s}' is required but was not provided", .{@tagName(first)});
            } else {
                try writer.print("required flags not provided: '--{s}'", .{@tagName(first)});
            }
            while (it.next()) |flag| {
                try writer.print(", '--{s}'", .{@tagName(flag)});
            }
        },
        .expected_integer => |bytes| writer.print("expected integer value for '{s}'", .{bytes}),
        .expected_program_name => writer.writeAll("expected at least one argument"),
        .expected_value => |arg| writer.print("expected value for flag '{s}'", .{arg}),
        .invalid_short_position => |short| writer.print("expected value for short '-{s}'", .{&.{short}}),
        .switch_value => |arg| writer.print("cannot have value for switch '{s}'", .{arg}),
        .unexpected_end_of_input => writer.writeAll("unexpected end of input"),
        .unexpected_positional => |s| writer.print("unexpected positional argument '{s}'", .{s}),
        .unknown_flag => |arg| writer.print("unknown flag '{s}'", .{arg}),
    };
}

fn setFlagValue(
    comptime T: type,
    comptime ArgsT: type,
    result: *T,
    comptime flag: Flag,
    flag_input: []const u8,
    value: ?[]const u8,
    args: *ArgsT,
) ?ParseError(T) {
    switch (flag.type) {
        .boolean => {
            if (value != null) {
                return .{ .switch_value = flag_input };
            }
            @field(result, flag.name) = true;
        },
        .bytes => {
            const raw_value = value orelse args.next() orelse
                return .{ .expected_value = flag_input };
            @field(result, flag.name) = raw_value;
        },
        .integer => |t| {
            const raw_value = value orelse args.next() orelse
                return .{ .expected_value = flag_input };
            @field(result, flag.name) = std.fmt.parseInt(t, raw_value, 10) catch
                return .{ .expected_integer = flag_input };
        },
        .@"enum" => |e| {
            const raw_value = value orelse args.next() orelse
                return .{ .expected_value = flag_input };
            var variant_found = false;
            inline for (e) |field| {
                if (std.ascii.eqlIgnoreCase(field.name, raw_value)) {
                    variant_found = true;
                    @field(result, flag.name) = @field(flag.raw_type, field.name);
                }
            }
            if (!variant_found) {
                return .{ .enum_variant_not_found = raw_value };
            }
        },
    }
    return null;
}

const SliceIterator = struct {
    inner: []const []const u8,
    index: usize = 0,

    pub fn next(self: *SliceIterator) ?[]const u8 {
        if (self.index == self.inner.len) {
            return null;
        }
        const s = self.inner[self.index];
        self.index += 1;
        return s;
    }
};

pub fn parse(
    comptime T: type,
    allocator: Allocator,
    slice: []const []const u8,
) Allocator.Error!ParseResult(T) {
    return parseFromIterator(T, SliceIterator{ .inner = slice }, allocator);
}

pub fn freeFlags(allocator: Allocator, flags: anytype) void {
    const info = comptime getInfo(@TypeOf(flags));
    if (info.variadic) |variadic| {
        allocator.free(@field(flags, variadic.name));
    }
}

const TypeInfo = struct {
    flags: []const Flag,
    positionals: []const Positional,
    variadic: ?Variadic,
};

const Flag = struct {
    name: []const u8,
    short: ?u8,
    type: FlagType,
    raw_type: type,
    standalone: bool,
    default_value: ?*const anyopaque,

    pub fn isOptional(comptime self: *const Flag) bool {
        return self.default_value != null or
            @typeInfo(self.raw_type) == .optional or
            self.type == .boolean;
    }
};

const FlagType = union(enum) {
    bytes: void,
    boolean: void,
    @"enum": []const std.builtin.Type.EnumField,
    integer: type,
};

const Positional = struct {
    name: []const u8,
    raw_type: type,
    default_value: ?*const anyopaque,

    pub fn isOptional(comptime self: *const Positional) bool {
        return self.default_value != null or @typeInfo(self.raw_type) == .optional;
    }
};

const Variadic = struct {
    name: []const u8,
};

fn getInfo(comptime T: type) TypeInfo {
    const info: TypeInfo = comptime info: {
        var flags: []const Flag = &.{};
        var positionals: []const Positional = &.{};
        var variadic: ?Variadic = null;

        var flag_fields: std.EnumSet(std.meta.FieldEnum(T)) = .initFull();
        if (@hasDecl(T, "positionals")) {
            const pos_fields = @typeInfo(@TypeOf(T.positionals)).@"struct".fields;
            var found_default = false;
            for (pos_fields) |pos_field| {
                if (!@hasField(T, pos_field.name)) {
                    @compileError(std.fmt.comptimePrint("positional field `{s}` does not exist", .{pos_field.name}));
                }
                const field: std.builtin.Type.StructField = b: {
                    for (@typeInfo(T).@"struct".fields) |f| {
                        if (std.mem.eql(u8, f.name, pos_field.name)) {
                            break :b f;
                        }
                    }
                };
                if (unwrapOptional(field.type) != []const u8) {
                    @compileError("positional fields should be []const u8");
                }
                if (field.default_value_ptr != null) {
                    found_default = true;
                } else if (found_default) {
                    @compileError("positional with default values should be trailing");
                }
                positionals = positionals ++ .{Positional{
                    .name = pos_field.name,
                    .raw_type = field.type,
                    .default_value = field.default_value_ptr,
                }};
                flag_fields.remove(@field(std.meta.FieldEnum(T), pos_field.name));
            }
        }

        var shorts: std.EnumMap(std.meta.FieldEnum(T), u8) = .init(.{});
        if (@hasDecl(T, "shorts")) {
            for (@typeInfo(@TypeOf(T.shorts)).@"struct".fields) |sw| {
                const value = sw.defaultValue() orelse @compileError("need switch value");
                if (!std.ascii.isAscii(value)) {
                    @compileError(std.fmt.comptimePrint(
                        "shorts must be ASCII characters only, got '{c}'",
                        .{value},
                    ));
                }
                if (value == '-') {
                    @compileError("shorts cannot be `-`");
                }
                if (@hasField(T, &.{value})) {
                    @compileError("shorts cannot conflict with flags");
                }
                var it = shorts.iterator();
                while (it.next()) |entry| {
                    if (value == entry.value.*) {
                        @compileError("shorts cannot conflict with each other");
                    }
                }
                shorts.put(@field(std.meta.FieldEnum(T), sw.name), value);
            }
        }

        var standalone: std.EnumSet(std.meta.FieldEnum(T)) = .initEmpty();
        if (@hasDecl(T, "standalone")) {
            for (@typeInfo(@TypeOf(T.standalone)).@"struct".fields) |flag| {
                const f = @field(std.meta.FieldEnum(T), flag.name);
                if (!flag_fields.contains(f)) {
                    @compileError("standalone must correspond to a flag");
                }
                standalone.insert(f);
            }
        }

        for (@typeInfo(T).@"struct".fields) |field| {
            // Skip if we have already handled this field (i.e. it is a positional)
            if (!flag_fields.contains(@field(std.meta.FieldEnum(T), field.name))) {
                continue;
            }
            // Handle special variadic field
            if (field.type == []const []const u8 or field.type == [][]const u8) {
                if (positionals.len > 0 and positionals[positionals.len - 1].default_value != null) {
                    @compileError("cannot have variadic argument with default positional arguments");
                }
                variadic = .{ .name = field.name };
                continue;
            }

            if (std.mem.indexOfScalar(u8, field.name, '=') != null) {
                @compileError("flag name cannot have `=` in it");
            }

            const flag_type: FlagType = switch (unwrapOptional(field.type)) {
                []const u8 => .bytes,
                bool => .boolean,
                else => switch (@typeInfo(unwrapOptional(field.type))) {
                    .int => .{ .integer = unwrapOptional(field.type) },
                    .@"enum" => |info| .{ .@"enum" = info.fields },
                    else => @compileError("unsupported flag type"),
                },
            };
            const f = @field(std.meta.FieldEnum(T), field.name);
            flags = flags ++ .{Flag{
                .name = field.name,
                .short = shorts.fetchRemove(f),
                .type = flag_type,
                .raw_type = field.type,
                .standalone = standalone.contains(f),
                .default_value = field.default_value_ptr,
            }};
        }

        var shorts_it = shorts.iterator();
        while (shorts_it.next()) |entry| {
            @compileError(std.fmt.comptimePrint("unmapped short: {s}", .{@tagName(entry.key)}));
        }
        break :info .{
            .flags = flags,
            .positionals = positionals,
            .variadic = variadic,
        };
    };
    return info;
}

fn unwrapOptional(comptime T: type) type {
    return switch (@typeInfo(T)) {
        .optional => |opt| opt.child,
        else => T,
    };
}

test parse {
    // There are only specific scenarios where we are allowed to allocate.
    var fa: std.testing.FailingAllocator = .init(
        std.testing.allocator,
        .{ .fail_index = 0, .resize_fail_index = 0 },
    );

    const Example = struct {
        positional1: []const u8,
        positional2: []const u8,
        optional_positional: ?[]const u8,

        flag: u32,
        default_flag: i32 = -5,
        optional: ?[]const u8,

        pub const positionals = .{
            .positional1 = void,
            .positional2 = void,
            .optional_positional = void,
        };
        pub const shorts = .{
            .flag = 'f',
            .default_flag = 'x',
        };
    };
    try std.testing.expectEqualDeep(ParseResult(Example){ .flags = Example{
        .positional1 = "hello",
        .positional2 = "hi",
        .optional_positional = null,
        .flag = 68,
        .default_flag = -5,
        .optional = "nice",
    } }, parse(
        Example,
        fa.allocator(),
        &.{ "hello", "hi", "--flag", "68", "--optional=nice" },
    ));
    try std.testing.expectEqual(ParseResult(Example){ .flags = Example{
        .positional1 = "10",
        .positional2 = "15",
        .optional_positional = "20",
        .flag = 32,
        .default_flag = 55,
        .optional = null,
    } }, parse(
        Example,
        fa.allocator(),
        &.{ "10", "15", "-f", "32", "20", "-x=55" },
    ));

    const Example2 = struct {
        args: []const []const u8,
        f1: bool,
        f2: bool,
        f3: bool,
        message: ?[]const u8 = null,

        const shorts = .{
            .f1 = 'a',
            .f2 = 'b',
            .f3 = 'c',
            .message = 'm',
        };
    };
    const result = parse(Example2, std.testing.allocator, &.{
        "hello",
        "-ab",
        "world",
    }) catch @panic("OOM");
    defer freeFlags(std.testing.allocator, result.flags);
    try std.testing.expectEqualDeep(ParseResult(Example2){ .flags = Example2{
        .args = &.{ "hello", "world" },
        .f1 = true,
        .f2 = true,
        .f3 = false,
        .message = null,
    } }, result);
    try std.testing.expectEqualDeep(ParseResult(Example2){ .flags = Example2{
        .args = &.{},
        .f1 = true,
        .f2 = false,
        .f3 = true,
        .message = "hi",
    } }, parse(Example2, fa.allocator(), &.{
        "-am", "hi", "--f3",
    }));
    try std.testing.expectEqualDeep(ParseResult(Example2){ .flags = Example2{
        .args = &.{},
        .f1 = true,
        .f2 = false,
        .f3 = false,
        .message = "hello",
    } }, parse(Example2, fa.allocator(), &.{
        "-am=hello",
    }));

    const Example3 = struct {
        quality: enum {
            good,
            bad,
            medium,
        } = .medium,
        const shorts = .{
            .quality = 'q',
        };
    };
    try std.testing.expectEqualDeep(ParseResult(Example3){ .flags = Example3{
        .quality = .good,
    } }, parse(Example3, fa.allocator(), &.{ "--quality", "good" }));
    try std.testing.expectEqualDeep(ParseResult(Example3){ .flags = Example3{
        .quality = .medium,
    } }, parse(Example3, fa.allocator(), &.{}));
    try std.testing.expectEqualDeep(ParseResult(Example3){ .flags = Example3{
        .quality = .bad,
    } }, parse(Example3, fa.allocator(), &.{"--quality=bad"}));
    try std.testing.expectEqualDeep(ParseResult(Example3){ .flags = Example3{
        .quality = .good,
    } }, parse(Example3, fa.allocator(), &.{"-q=good"}));
    try std.testing.expectEqualDeep(ParseResult(Example3){ .flags = Example3{
        .quality = .good,
    } }, parse(Example3, fa.allocator(), &.{ "-q", "good" }));

    const Example4 = struct {
        required: []const u8,
        help: bool,
        const standalone = .{
            .help = void,
        };
    };
    const example4 = parse(Example4, fa.allocator(), &.{"--help"}) catch @panic("unreachable");
    try std.testing.expect(example4 == .flags);
    try std.testing.expect(example4.flags.help);
}
