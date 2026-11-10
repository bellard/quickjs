/* Run in a fresh process with the TZ values specified by the Makefile.
   PST8PDT uses the US DST rules in the Windows CRT. */
function assert(actual, expected, message)
{
    if (actual !== expected)
        throw Error(message + ": got " + actual + ", expected " + expected);
}

function check(utc, offset, local)
{
    var date = new Date(utc);
    assert(date.getTimezoneOffset(), offset, utc + " offset");
    assert(date.getFullYear(), local[0], utc + " year");
    assert(date.getMonth() + 1, local[1], utc + " month");
    assert(date.getDate(), local[2], utc + " day");
    assert(date.getHours(), local[3], utc + " hour");
    assert(date.getMinutes(), local[4], utc + " minute");
    assert(date.getSeconds(), local[5], utc + " second");
    assert(date.toISOString(), utc, "UTC value must not change");
    assert(Date.parse(date.toString()), date.getTime(), utc + " string round trip");
}

switch (scriptArgs[1]) {
case "pacific":
    check("2024-01-15T12:00:00.000Z", 480, [2024, 1, 15, 4, 0, 0]);
    check("2024-07-15T12:00:00.000Z", 420, [2024, 7, 15, 5, 0, 0]);
    // Both sides of the skipped and repeated local hour, at second precision.
    check("2024-03-10T09:59:59.000Z", 480, [2024, 3, 10, 1, 59, 59]);
    check("2024-03-10T10:00:00.000Z", 420, [2024, 3, 10, 3, 0, 0]);
    check("2024-11-03T08:59:59.000Z", 420, [2024, 11, 3, 1, 59, 59]);
    check("2024-11-03T09:00:00.000Z", 480, [2024, 11, 3, 1, 0, 0]);
    // UTC and local dates differ, including across leap-year boundaries.
    check("2024-01-01T00:00:00.000Z", 480, [2023, 12, 31, 16, 0, 0]);
    check("2025-01-01T00:00:00.000Z", 480, [2024, 12, 31, 16, 0, 0]);
    check("2024-03-01T00:00:00.000Z", 480, [2024, 2, 29, 16, 0, 0]);
    assert(new Date(2024, 6, 15, 5).toISOString(),
           "2024-07-15T12:00:00.000Z", "local constructor during DST");
    break;
case "east":
    // TZ=NPT-5:45: fractional-hour offset and the opposite date rollover.
    check("2023-12-31T23:30:00.000Z", -345, [2024, 1, 1, 5, 15, 0]);
    check("2024-12-31T23:30:00.000Z", -345, [2025, 1, 1, 5, 15, 0]);
    check("2024-02-28T23:30:00.000Z", -345, [2024, 2, 29, 5, 15, 0]);
    check("2024-02-29T23:30:00.000Z", -345, [2024, 3, 1, 5, 15, 0]);
    break;
case "utc":
    check("2024-01-01T00:00:00.000Z", 0, [2024, 1, 1, 0, 0, 0]);
    check("2024-07-15T12:00:00.000Z", 0, [2024, 7, 15, 12, 0, 0]);
    break;
default:
    throw Error("Expected pacific, east or utc test case");
}
