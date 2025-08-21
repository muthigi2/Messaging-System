# syntax=docker/dockerfile:1
# Build stage
FROM node:18-bullseye AS build
WORKDIR /app
COPY package*.json ./
RUN npm ci
COPY . .
RUN npm run build

# Serve stage
FROM nginx:alpine
COPY --from=build /app/build /usr/share/nginx/html
# Optional: custom nginx config can be added if needed
EXPOSE 80
CMD ["nginx", "-g", "daemon off;"] 