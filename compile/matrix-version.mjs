#!/usr/bin/env node
// Resolves the version compile/Makefile should build for a given
// matrix.json library entry — its "latest" value (the last entry of that
// library's `versions` array, per the header comment in matrix.json).
// Ported from php-wasm-compiler's own matrix-version.mjs (same mechanism,
// requested for this project too — see CLAUDE.md).
// Used two ways: as a CLI (`node matrix-version.mjs <lib>`, called from
// Makefile via `$(shell ...)`) and as a module
// (`import { getMatrixVersion } from './matrix-version.mjs'`).
import { readFileSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const sourceDir = path.dirname(fileURLToPath(import.meta.url));
const matrixPath = path.resolve(sourceDir, '..', 'matrix.json');

export function getMatrixVersion(libraryKey) {
	const matrix = JSON.parse(readFileSync(matrixPath, 'utf8'));
	const lib = matrix.libraries?.[libraryKey];
	if (!lib) {
		throw new Error(`matrix.json: no "libraries.${libraryKey}" entry.`);
	}
	const versions = lib.versions ?? [];
	if (versions.length === 0) {
		throw new Error(`matrix.json: "libraries.${libraryKey}.versions" is empty.`);
	}
	return versions[versions.length - 1];
}

if (process.argv[1] === fileURLToPath(import.meta.url)) {
	const libraryKey = process.argv[2];
	if (!libraryKey) {
		console.error('Usage: node matrix-version.mjs <libraryKey>');
		process.exit(1);
	}
	try {
		console.log(getMatrixVersion(libraryKey));
	} catch (error) {
		console.error(error.message);
		process.exit(1);
	}
}
